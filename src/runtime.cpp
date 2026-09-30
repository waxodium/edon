#include "runtime.hpp"
#include "runtime_output.hpp"
#include "api.hpp"
#include "native_io.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

void setRequireError(
    JSContextRef context,
    JSValueRef *error,
    const char *message) {

    if (!error)
        return;

    JSStringRef text = JSStringCreateWithUTF8CString(message);
    JSValueRef argument = JSValueMakeString(context, text);

    *error = JSObjectMakeError(
        context,
        1,
        &argument,
        nullptr);

    JSStringRelease(text);
}

JSValueRef jsRequire(
    JSContextRef context,
    JSObjectRef,
    JSObjectRef,
    std::size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef *error) {

    if (argumentCount != 1 ||
        !JSValueIsString(context, arguments[0])) {

        setRequireError(
            context,
            error,
            "require() requires exactly one module name string");

        return JSValueMakeUndefined(context);
    }

    JSStringRef text =
        JSValueToStringCopy(context, arguments[0], error);

    if (!text)
        return JSValueMakeUndefined(context);

    const std::size_t maxSize =
        JSStringGetMaximumUTF8CStringSize(text);

    std::string name(maxSize, '\0');

    const std::size_t size =
        JSStringGetUTF8CString(
            text,
            name.data(),
            maxSize);

    JSStringRelease(text);

    if (size == 0) {
        setRequireError(
            context,
            error,
            "require() received an invalid module name");

        return JSValueMakeUndefined(context);
    }

    name.resize(size - 1);

    return edon::ffi::requireModule(
        context,
        name,
        error);
}

void registerRequire(JSGlobalContextRef context) {
    JSObjectRef global =
        JSContextGetGlobalObject(context);

    JSStringRef name =
        JSStringCreateWithUTF8CString("require");

    JSObjectRef function =
        JSObjectMakeFunctionWithCallback(
            context,
            name,
            jsRequire);

    if (function) {
        JSObjectSetProperty(
            context,
            global,
            name,
            function,
            kJSPropertyAttributeNone,
            nullptr);
    }

    JSStringRelease(name);
}

} // namespace

EdonRuntime::EdonRuntime() {
    edon::initSystemIO();

    context = JSGlobalContextCreate(nullptr);

    edon::registerNativeIO(context);
    edon::ffi::registerNamespace(context);

    registerRequire(context);
    bootstrapCoreLibraries();
}

EdonRuntime::~EdonRuntime() {
    if (!context)
        return;

    JSGlobalContextRelease(context);
    context = nullptr;
}

void EdonRuntime::bootstrapCoreLibraries() {
    const char *bootstrapJS = R"js(
        (function() {
            const styles = {
                bold: "\x1b[1m", reset: "\x1b[0m", gray: "\x1b[90m",
                yellow: "\x1b[33m", green: "\x1b[32m", cyan: "\x1b[36m",
                red: "\x1b[31m", magenta: "\x1b[35m"
            };

            function safeGetPropertyDescriptor(obj, key) {
                try {
                    let cur = obj;
                    while (cur !== null && cur !== undefined) {
                        const desc = Object.getOwnPropertyDescriptor(cur, key);
                        if (desc) {
                            return (desc.get || desc.set)
                                ? { type: "accessor" }
                                : { type: "value", value: desc.value };
                        }
                        cur = Object.getPrototypeOf(cur);
                    }

                    return {
                        type: "value",
                        value: obj[key]
                    };
                } catch (e) {
                    return { type: "error" };
                }
            }

            function inspect(value, opts = {}, seen = new Set()) {
                const useColors =
                    opts.colors !== undefined
                        ? opts.colors
                        : __edon_isatty(1);

                const colorize = (style, text) =>
                    useColors
                        ? `${styles[style]}${text}${styles.reset}`
                        : text;

                const maxDepth =
                    opts.depth !== undefined
                        ? opts.depth
                        : 2;

                const currentDepth =
                    opts._currentDepth || 0;

                if (value === null)
                    return colorize("bold", "null");

                if (value === undefined)
                    return colorize("gray", "undefined");

                if (
                    typeof value === "boolean" ||
                    typeof value === "number"
                ) {
                    return colorize("yellow", String(value));
                }

                if (typeof value === "bigint")
                    return colorize("green", `${value}n`);

                if (typeof value === "string") {
                    return opts.inObj
                        ? colorize("green", `'${value}'`)
                        : value;
                }

                if (typeof value === "symbol")
                    return colorize("green", value.toString());

                if (typeof value === "function") {
                    return colorize(
                        "cyan",
                        `[Function: ${value.name || "anonymous"}]`
                    );
                }

                if (
                    typeof value === "object" &&
                    value !== null
                ) {
                    if (seen.has(value))
                        return colorize("cyan", "[Circular]");

                    seen.add(value);
                }

                try {
                    if (value instanceof ArrayBuffer) {
                        return colorize(
                            "cyan",
                            `ArrayBuffer { byteLength: ${value.byteLength} }`
                        );
                    }

                    if (ArrayBuffer.isView(value)) {
                        return colorize(
                            "cyan",
                            `${value.constructor.name}(${value.byteLength})`
                        );
                    }

                    if (value instanceof Date)
                        return colorize(
                            "magenta",
                            value.toISOString()
                        );

                    if (value instanceof RegExp)
                        return colorize(
                            "red",
                            value.toString()
                        );

                    if (value instanceof Error) {
                        const name = value.name || "Error";
                        const msg =
                            value.message
                                ? `: ${value.message}`
                                : "";

                        const stack =
                            value.stack
                                ? `\n    at ${value.stack}`
                                : "";

                        return colorize(
                            "red",
                            `${name}${msg}${stack}`
                        );
                    }

                    if (value instanceof Map) {
                        if (currentDepth > maxDepth)
                            return colorize("cyan", "[Map]");

                        const entries = [];

                        value.forEach((v, k) => {
                            entries.push(
                                `${inspect(
                                    k,
                                    {...opts, inObj: true},
                                    seen
                                )} => ${inspect(
                                    v,
                                    {...opts, inObj: true},
                                    seen
                                )}`
                            );
                        });

                        return `Map(${value.size}) { ${entries.join(", ")} }`;
                    }

                    if (value instanceof Set) {
                        if (currentDepth > maxDepth)
                            return colorize("cyan", "[Set]");

                        const items = Array.from(value).map(
                            v => inspect(
                                v,
                                {...opts, inObj: true},
                                seen
                            )
                        );

                        return `Set(${value.size}) { ${items.join(", ")} }`;
                    }

                    if (currentDepth > maxDepth) {
                        return Array.isArray(value)
                            ? colorize("cyan", "[Array]")
                            : colorize("cyan", "[Object]");
                    }

                    const childOpts = {
                        ...opts,
                        _currentDepth: currentDepth + 1,
                        inObj: true
                    };

                    if (Array.isArray(value)) {
                        const items = [];

                        for (let i = 0; i < value.length; i++) {
                            items.push(
                                inspect(
                                    value[i],
                                    childOpts,
                                    seen
                                )
                            );
                        }

                        const customKeys =
                            Reflect.ownKeys(value).filter(
                                k =>
                                    k !== "length" &&
                                    isNaN(Number(k))
                            );

                        for (const k of customKeys) {
                            const prop =
                                safeGetPropertyDescriptor(
                                    value,
                                    k
                                );

                            const keyStr =
                                typeof k === "symbol"
                                    ? colorize("green", k.toString())
                                    : k;

                            const valStr =
                                prop.type === "accessor"
                                    ? colorize(
                                        "cyan",
                                        "[Getter/Setter]"
                                    )
                                    : inspect(
                                        prop.value,
                                        childOpts,
                                        seen
                                    );

                            items.push(`${keyStr}: ${valStr}`);
                        }

                        return `[ ${items.join(", ")} ]`;
                    }

                    const keys = Reflect.ownKeys(value);

                    const elements = keys.map(k => {
                        const prop =
                            safeGetPropertyDescriptor(
                                value,
                                k
                            );

                        const keyStr =
                            typeof k === "symbol"
                                ? colorize("green", k.toString())
                                : k;

                        const valStr =
                            prop.type === "accessor"
                                ? colorize(
                                    "cyan",
                                    "[Getter/Setter]"
                                )
                                : inspect(
                                    prop.value,
                                    childOpts,
                                    seen
                                );

                        return `${keyStr}: ${valStr}`;
                    });

                    return `{ ${elements.join(", ")} }`;
                } finally {
                    seen.delete(value);
                }
            }

            function format(...args) {
                if (typeof args[0] === "string") {
                    if (args.length === 1)
                        return args[0];

                    let i = 1;

                    const str = args[0].replace(
                        /%[sdj%]/g,
                        match => {
                            if (match === "%%")
                                return "%";

                            if (i >= args.length)
                                return match;

                            const value = args[i++];

                            if (match === "%s") {
                                return typeof value === "object"
                                    ? inspect(value)
                                    : String(value);
                            }

                            if (match === "%d")
                                return Number(value);

                            if (match === "%j") {
                                try {
                                    return JSON.stringify(value);
                                } catch (_) {
                                    return "[Circular]";
                                }
                            }

                            return match;
                        }
                    );

                    const remaining =
                        args.slice(i).map(a => inspect(a));

                    return remaining.length > 0
                        ? `${str} ${remaining.join(" ")}`
                        : str;
                }

                return args.map(a => inspect(a)).join(" ");
            }

            globalThis.util = {
                format,
                inspect
            };

            globalThis.console = {
                log: (...args) =>
                    __edon_write(1, format(...args) + "\n"),

                error: (...args) =>
                    __edon_write(2, format(...args) + "\n"),

                warn: (...args) =>
                    __edon_write(2, format(...args) + "\n"),

                info: (...args) =>
                    __edon_write(1, format(...args) + "\n"),

                assert: (assertion, ...args) => {
                    if (!assertion) {
                        const err = new Error(
                            "Assertion failed" +
                            (
                                args.length > 0
                                    ? ": " + format(...args)
                                    : ""
                            )
                        );

                        __edon_write(
                            2,
                            err.stack + "\n"
                        );
                    }
                }
            };
        })();
    )js";

    executeSource(bootstrapJS, "<bootstrap:core>");
}

bool EdonRuntime::executeSource(
    const std::string &source,
    const std::string &filename) {

    JSStringRef jsSource =
        JSStringCreateWithUTF8CString(source.c_str());

    JSStringRef jsFilename =
        JSStringCreateWithUTF8CString(filename.c_str());

    if (!jsSource || !jsFilename) {
        if (jsSource)
            JSStringRelease(jsSource);

        if (jsFilename)
            JSStringRelease(jsFilename);

        std::cerr
            << "Uncaught Error: Failed to create JavaScript source strings\n";

        return false;
    }

    JSValueRef error = nullptr;

    (void)JSEvaluateScript(
        context,
        jsSource,
        nullptr,
        jsFilename,
        1,
        &error);

    JSStringRelease(jsSource);
    JSStringRelease(jsFilename);

    if (error) {
        edon::printDiagnostic(
            source,
            filename,
            context,
            error);

        return false;
    }

    return true;
}

bool EdonRuntime::executeFile(const std::string &filepath) {
    std::ifstream file(filepath);

    if (!file.is_open()) {
        std::cerr
            << "edon error: Cannot open file "
            << filepath
            << '\n';

        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return executeSource(
        buffer.str(),
        filepath);
}
