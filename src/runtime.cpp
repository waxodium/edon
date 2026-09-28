#include "runtime.hpp"
#include "ffi.hpp"
#include "native_io.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SourceLocation {
  std::string filename;
  std::size_t line = 0;
  std::size_t column = 0;
};

std::string jsStringToUTF8(JSStringRef string) {
  if (!string)
    return {};

  const size_t maxSize = JSStringGetMaximumUTF8CStringSize(string);

  if (maxSize == 0)
    return {};

  std::string result(maxSize, '\0');

  const size_t actualSize =
      JSStringGetUTF8CString(string, result.data(), maxSize);

  if (actualSize == 0)
    return {};

  result.resize(actualSize - 1);
  return result;
}

std::string valueToString(JSContextRef context, JSValueRef value) {
  if (!value)
    return {};

  JSStringRef string = JSValueToStringCopy(context, value, nullptr);

  if (!string)
    return {};

  std::string result = jsStringToUTF8(string);

  JSStringRelease(string);

  return result;
}

std::string getStringProperty(JSContextRef context, JSObjectRef object,
                              const char *name) {
  JSStringRef propertyName = JSStringCreateWithUTF8CString(name);

  if (!propertyName)
    return {};

  JSValueRef value =
      JSObjectGetProperty(context, object, propertyName, nullptr);

  JSStringRelease(propertyName);

  return valueToString(context, value);
}

std::size_t getNumberProperty(JSContextRef context, JSObjectRef object,
                              const char *name) {
  JSStringRef propertyName = JSStringCreateWithUTF8CString(name);

  if (!propertyName)
    return 0;

  JSValueRef value =
      JSObjectGetProperty(context, object, propertyName, nullptr);

  JSStringRelease(propertyName);

  if (!value || !JSValueIsNumber(context, value))
    return 0;

  const double number = JSValueToNumber(context, value, nullptr);

  if (number < 1)
    return 0;

  return static_cast<std::size_t>(number);
}

std::string trim(const std::string &value) {
  std::size_t begin = 0;

  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  std::size_t end = value.size();

  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(begin, end - begin);
}

bool parseUnsigned(const std::string &text, std::size_t &position,
                   std::size_t &result) {
  const std::size_t start = position;

  while (position < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[position]))) {
    ++position;
  }

  if (position == start)
    return false;

  try {
    result = std::stoull(text.substr(start, position - start));
  } catch (...) {
    return false;
  }

  return true;
}

bool parseLocationSuffix(const std::string &text, SourceLocation &location) {
  if (text.empty())
    return false;

  std::size_t end = text.size();

  while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }

  if (end == 0)
    return false;

  std::size_t colon = text.rfind(':', end - 1);

  if (colon == std::string::npos)
    return false;

  std::size_t column = 0;
  std::size_t columnStart = colon + 1;

  if (columnStart >= end)
    return false;

  std::size_t cursor = columnStart;

  if (!parseUnsigned(text, cursor, column))
    return false;

  if (cursor != end)
    return false;

  if (colon == 0)
    return false;

  std::size_t secondColon = text.rfind(':', colon - 1);

  if (secondColon == std::string::npos)
    return false;

  std::size_t line = 0;
  std::size_t lineStart = secondColon + 1;

  cursor = lineStart;

  if (!parseUnsigned(text, cursor, line))
    return false;

  if (cursor != colon || line == 0)
    return false;

  std::string filename = text.substr(0, secondColon);

  if (filename.empty())
    return false;

  if (filename.rfind("at ", 0) == 0)
    filename = filename.substr(3);

  if (!filename.empty() && filename.front() == '(' && filename.back() == ')') {
    filename = filename.substr(1, filename.size() - 2);
  }

  location.filename = filename;
  location.line = line;
  location.column = column;

  return true;
}

bool parseStackLocation(const std::string &stack, SourceLocation &location) {
  std::istringstream stream(stack);
  std::string line;

  while (std::getline(stream, line)) {
    line = trim(line);

    if (line.empty())
      continue;

    if (parseLocationSuffix(line, location))
      return true;
  }

  return false;
}

bool parseCompilerLocation(const std::string &message,
                           SourceLocation &location) {
  std::istringstream stream(message);
  std::string line;

  while (std::getline(stream, line)) {
    line = trim(line);

    if (line.empty())
      continue;

    /*
     * TCC commonly reports diagnostics like:
     *
     *   <string>:3: error: 'c' undeclared
     *
     * or:
     *
     *   <string>:3:12: error: ...
     */

    const std::size_t marker = line.find(": error:");

    const std::size_t markerAlt = line.find(": error ");

    const std::size_t errorPos =
        marker != std::string::npos ? marker : markerAlt;

    if (errorPos == std::string::npos)
      continue;

    const std::string prefix = line.substr(0, errorPos);

    if (parseLocationSuffix(prefix, location))
      return true;

    /*
     * TCC's "<string>:3" form has no column.
     */
    const std::size_t colon = prefix.rfind(':');

    if (colon == std::string::npos)
      continue;

    std::size_t lineNumber = 0;
    std::size_t cursor = colon + 1;

    if (!parseUnsigned(prefix, cursor, lineNumber)) {
      continue;
    }

    if (cursor != prefix.size() || lineNumber == 0) {
      continue;
    }

    location.filename = prefix.substr(0, colon);

    location.line = lineNumber;
    location.column = 0;

    return true;
  }

  return false;
}

std::string sourceLine(const std::string &source, std::size_t lineNumber) {
  if (lineNumber == 0)
    return {};

  std::size_t currentLine = 1;
  std::size_t start = 0;

  while (start <= source.size()) {
    std::size_t end = source.find('\n', start);

    if (end == std::string::npos)
      end = source.size();

    if (currentLine == lineNumber) {
      std::string line = source.substr(start, end - start);

      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      return line;
    }

    if (end == source.size())
      break;

    start = end + 1;
    ++currentLine;
  }

  return {};
}

std::string sourceLineBefore(const std::string &source,
                             std::size_t lineNumber) {
  if (lineNumber <= 1)
    return {};

  return sourceLine(source, lineNumber - 1);
}

std::string sourceLineAfter(const std::string &source, std::size_t lineNumber) {
  return sourceLine(source, lineNumber + 1);
}

void printSourceContext(const std::string &source,
                        const SourceLocation &location) {
  if (location.line == 0)
    return;

  const std::string previous = sourceLineBefore(source, location.line);

  const std::string current = sourceLine(source, location.line);

  const std::string next = sourceLineAfter(source, location.line);

  if (current.empty() && previous.empty() && next.empty()) {
    return;
  }

  std::cerr << '\n';

  const std::size_t width = std::to_string(location.line).size();

  if (!previous.empty()) {
    std::cerr << "  " << std::string(width, ' ') << " | " << previous << '\n';
  }

  if (!current.empty()) {
    std::cerr << "> " << location.line << " | " << current << '\n';

    if (location.column > 0) {
      const std::size_t offset = location.column > 0 ? location.column - 1 : 0;

      std::cerr << "  " << std::string(width, ' ') << " | "
                << std::string(offset, ' ') << '^' << '\n';
    }
  }

  if (!next.empty()) {
    std::cerr << "  " << std::string(width, ' ') << " | " << next << '\n';
  }
}

std::string cleanCompilerMessage(const std::string &message) {
  std::istringstream stream(message);
  std::string line;

  while (std::getline(stream, line)) {
    line = trim(line);

    if (line.empty())
      continue;

    const std::size_t marker = line.find(": error:");

    const std::size_t markerAlt = line.find(": error ");

    if (marker != std::string::npos) {
      return trim(line.substr(marker + 8));
    }

    if (markerAlt != std::string::npos) {
      return trim(line.substr(markerAlt + 7));
    }
  }

  return trim(message);
}

void printDiagnostic(const std::string &source, const std::string &filename,
                     JSContextRef context, JSValueRef error) {
  if (!error) {
    std::cerr << "Uncaught Exception: <unknown>\n";
    return;
  }

  std::string name = "Error";
  std::string message;
  std::string stack;

  if (JSValueIsObject(context, error)) {
    JSObjectRef object = JSValueToObject(context, error, nullptr);

    if (object) {
      const std::string propertyName =
          getStringProperty(context, object, "name");

      const std::string propertyMessage =
          getStringProperty(context, object, "message");

      const std::string propertyStack =
          getStringProperty(context, object, "stack");

      if (!propertyName.empty())
        name = propertyName;

      if (!propertyMessage.empty())
        message = propertyMessage;

      stack = propertyStack;

      SourceLocation location;

      const std::size_t line = getNumberProperty(context, object, "line");

      const std::size_t column = getNumberProperty(context, object, "column");

      if (line > 0) {
        location.filename = filename;
        location.line = line;
        location.column = column;
      } else {
        parseStackLocation(stack, location);
      }

      if (location.filename.empty())
        location.filename = filename;

      if (message.empty())
        message = valueToString(context, error);

      /*
       * TCC diagnostics can be embedded in the
       * exception message. Recover their source
       * location when JSC did not provide one.
       */
      if (location.line == 0) {
        parseCompilerLocation(message, location);

        if (location.filename.empty())
          location.filename = filename;
      }

      std::string displayMessage = cleanCompilerMessage(message);

      std::cerr << "Uncaught " << name;

      if (!displayMessage.empty())
        std::cerr << ": " << displayMessage;

      std::cerr << '\n';

      if (location.line > 0) {
        std::cerr << "\n  at " << location.filename << ':' << location.line;

        if (location.column > 0)
          std::cerr << ':' << location.column;

        std::cerr << '\n';

        printSourceContext(source, location);
      }

      /*
       * Keep the original stack for exceptions where
       * the location is not already represented by the
       * source excerpt.
       */
      if (!stack.empty() && location.line == 0) {
        std::cerr << '\n' << stack << '\n';
      }

      return;
    }
  }

  message = valueToString(context, error);

  if (message.empty())
    message = "<unknown>";

  std::cerr << "Uncaught " << name << ": " << message << '\n';
}

JSValueRef jsRequire(JSContextRef context, JSObjectRef, JSObjectRef,
                     size_t argumentCount, const JSValueRef arguments[],
                     JSValueRef *error) {
  if (argumentCount != 1) {
    JSStringRef message = JSStringCreateWithUTF8CString(
        "require() requires exactly one module name");

    if (message) {
      JSValueRef args[] = {JSValueMakeString(context, message)};

      if (error) {
        *error = JSObjectMakeError(context, 1, args, nullptr);
      }

      JSStringRelease(message);
    }

    return JSValueMakeUndefined(context);
  }

  if (!JSValueIsString(context, arguments[0])) {
    JSStringRef message =
        JSStringCreateWithUTF8CString("require() module name must be a string");

    if (message) {
      JSValueRef args[] = {JSValueMakeString(context, message)};

      if (error) {
        *error = JSObjectMakeError(context, 1, args, nullptr);
      }

      JSStringRelease(message);
    }

    return JSValueMakeUndefined(context);
  }

  JSStringRef string = JSValueToStringCopy(context, arguments[0], error);

  if (!string)
    return JSValueMakeUndefined(context);

  const size_t maxSize = JSStringGetMaximumUTF8CStringSize(string);

  if (maxSize == 0) {
    JSStringRelease(string);

    if (error) {
      JSStringRef message = JSStringCreateWithUTF8CString(
          "require() received an invalid module name");

      if (message) {
        JSValueRef args[] = {JSValueMakeString(context, message)};

        *error = JSObjectMakeError(context, 1, args, nullptr);

        JSStringRelease(message);
      }
    }

    return JSValueMakeUndefined(context);
  }

  std::string name(maxSize, '\0');

  const size_t actualSize =
      JSStringGetUTF8CString(string, name.data(), maxSize);

  JSStringRelease(string);

  if (actualSize == 0) {
    if (error) {
      JSStringRef message = JSStringCreateWithUTF8CString(
          "require() received an invalid module name");

      if (message) {
        JSValueRef args[] = {JSValueMakeString(context, message)};

        *error = JSObjectMakeError(context, 1, args, nullptr);

        JSStringRelease(message);
      }
    }

    return JSValueMakeUndefined(context);
  }

  name.resize(actualSize - 1);

  return edon::ffi::requireModule(context, name, error);
}

void registerRequire(JSGlobalContextRef context) {
  JSObjectRef global = JSContextGetGlobalObject(context);

  JSStringRef propertyName = JSStringCreateWithUTF8CString("require");

  JSStringRef functionName = JSStringCreateWithUTF8CString("require");

  if (!propertyName || !functionName) {
    if (propertyName)
      JSStringRelease(propertyName);

    if (functionName)
      JSStringRelease(functionName);

    return;
  }

  JSObjectRef function =
      JSObjectMakeFunctionWithCallback(context, functionName, jsRequire);

  if (function) {
    JSObjectSetProperty(context, global, propertyName, function,
                        kJSPropertyAttributeNone, nullptr);
  }

  JSStringRelease(functionName);
  JSStringRelease(propertyName);
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
  if (context) {
    JSGlobalContextRelease(context);
    context = nullptr;
  }
}

void EdonRuntime::bootstrapCoreLibraries() {
  const char *bootstrapJS = R"js(
        (function() {
            const styles = {
                bold: "\x1b[1m",
                reset: "\x1b[0m",
                gray: "\x1b[90m",
                yellow: "\x1b[33m",
                green: "\x1b[32m",
                cyan: "\x1b[36m",
                red: "\x1b[31m",
                magenta: "\x1b[35m"
            };

            function safeGetPropertyDescriptor(obj, key) {
                try {
                    let cur = obj;

                    while (cur !== null && cur !== undefined) {
                        const desc =
                            Object.getOwnPropertyDescriptor(
                                cur,
                                key
                            );

                        if (desc) {
                            if (desc.get || desc.set) {
                                return { type: "accessor" };
                            }

                            return {
                                type: "value",
                                value: desc.value
                            };
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

            function inspect(
                value,
                opts = {},
                seen = new Set()
            ) {
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
                    return colorize(
                        "gray",
                        "undefined"
                    );

                if (
                    typeof value === "boolean" ||
                    typeof value === "number"
                ) {
                    return colorize(
                        "yellow",
                        String(value)
                    );
                }

                if (typeof value === "bigint")
                    return colorize(
                        "green",
                        `${value}n`
                    );

                if (typeof value === "string") {
                    return opts.inObj
                        ? colorize(
                            "green",
                            `'${value}'`
                        )
                        : value;
                }

                if (typeof value === "symbol")
                    return colorize(
                        "green",
                        value.toString()
                    );

                if (typeof value === "function")
                    return colorize(
                        "cyan",
                        `[Function: ${
                            value.name || "anonymous"
                        }]`
                    );

                if (
                    typeof value === "object" &&
                    value !== null
                ) {
                    if (seen.has(value)) {
                        return colorize(
                            "cyan",
                            "[Circular]"
                        );
                    }

                    seen.add(value);
                }

                try {
                    if (value instanceof ArrayBuffer) {
                        return colorize(
                            "cyan",
                            `ArrayBuffer { byteLength: ${
                                value.byteLength
                            } }`
                        );
                    }

                    if (ArrayBuffer.isView(value)) {
                        return colorize(
                            "cyan",
                            `${value.constructor.name}(${
                                value.byteLength
                            })`
                        );
                    }

                    if (value instanceof Error) {
                        const name =
                            value.name || "Error";

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

                    if (value instanceof Date) {
                        return colorize(
                            "magenta",
                            value.toISOString()
                        );
                    }

                    if (value instanceof RegExp) {
                        return colorize(
                            "red",
                            value.toString()
                        );
                    }

                    if (value instanceof Map) {
                        if (currentDepth > maxDepth) {
                            return colorize(
                                "cyan",
                                "[Map]"
                            );
                        }

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

                        return `Map(${value.size}) { ${
                            entries.join(", ")
                        } }`;
                    }

                    if (value instanceof Set) {
                        if (currentDepth > maxDepth) {
                            return colorize(
                                "cyan",
                                "[Set]"
                            );
                        }

                        const items =
                            Array.from(value).map(v =>
                                inspect(
                                    v,
                                    {
                                        ...opts,
                                        inObj: true
                                    },
                                    seen
                                )
                            );

                        return `Set(${value.size}) { ${
                            items.join(", ")
                        } }`;
                    }

                    if (currentDepth > maxDepth) {
                        return Array.isArray(value)
                            ? colorize(
                                "cyan",
                                "[Array]"
                            )
                            : colorize(
                                "cyan",
                                "[Object]"
                            );
                    }

                    const childOpts = {
                        ...opts,
                        _currentDepth:
                            currentDepth + 1,
                        inObj: true
                    };

                    if (Array.isArray(value)) {
                        const items = [];

                        for (
                            let i = 0;
                            i < value.length;
                            i++
                        ) {
                            items.push(
                                inspect(
                                    value[i],
                                    childOpts,
                                    seen
                                )
                            );
                        }

                        const customKeys =
                            Reflect.ownKeys(value)
                                .filter(
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
                                    ? colorize(
                                        "green",
                                        k.toString()
                                    )
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

                            items.push(
                                `${keyStr}: ${valStr}`
                            );
                        }

                        return `[ ${
                            items.join(", ")
                        } ]`;
                    }

                    const keys =
                        Reflect.ownKeys(value);

                    const elements =
                        keys.map(k => {
                            const prop =
                                safeGetPropertyDescriptor(
                                    value,
                                    k
                                );

                            const keyStr =
                                typeof k === "symbol"
                                    ? colorize(
                                        "green",
                                        k.toString()
                                    )
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

                    return `{ ${
                        elements.join(", ")
                    } }`;
                } finally {
                    seen.delete(value);
                }
            }

            function format(...args) {
                if (typeof args[0] === "string") {
                    if (args.length === 1)
                        return args[0];

                    let i = 1;

                    const str =
                        args[0].replace(
                            /%[sdj%]/g,
                            match => {
                                if (match === "%%")
                                    return "%";

                                if (i >= args.length)
                                    return match;

                                const val = args[i++];

                                if (match === "%s") {
                                    return typeof val === "object"
                                        ? inspect(val)
                                        : String(val);
                                }

                                if (match === "%d")
                                    return Number(val);

                                if (match === "%j") {
                                    try {
                                        return JSON.stringify(val);
                                    } catch (_) {
                                        return "[Circular]";
                                    }
                                }

                                return match;
                            }
                        );

                    const remaining =
                        args
                            .slice(i)
                            .map(a => inspect(a));

                    return remaining.length > 0
                        ? `${str} ${
                            remaining.join(" ")
                        }`
                        : str;
                }

                return args
                    .map(a => inspect(a))
                    .join(" ");
            }

            globalThis.util = {
                format,
                inspect
            };

            globalThis.console = {
                log: (...args) =>
                    __edon_write(
                        1,
                        format(...args) + "\n"
                    ),

                error: (...args) =>
                    __edon_write(
                        2,
                        format(...args) + "\n"
                    ),

                warn: (...args) =>
                    __edon_write(
                        2,
                        format(...args) + "\n"
                    ),

                info: (...args) =>
                    __edon_write(
                        1,
                        format(...args) + "\n"
                    ),

                assert: (assertion, ...args) => {
                    if (!assertion) {
                        const err =
                            new Error(
                                "Assertion failed" +
                                (
                                    args.length > 0
                                        ? ": " +
                                          format(...args)
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

bool EdonRuntime::executeSource(const std::string &source,
                                const std::string &filename) {
  JSStringRef jsSource = JSStringCreateWithUTF8CString(source.c_str());

  JSStringRef jsFilename = JSStringCreateWithUTF8CString(filename.c_str());

  if (!jsSource || !jsFilename) {
    if (jsSource)
      JSStringRelease(jsSource);

    if (jsFilename)
      JSStringRelease(jsFilename);

    std::cerr << "Uncaught Error: Failed to create "
                 "JavaScript source strings\n";

    return false;
  }

  JSValueRef error = nullptr;

  (void)JSEvaluateScript(context, jsSource, nullptr, jsFilename, 1, &error);

  JSStringRelease(jsSource);
  JSStringRelease(jsFilename);

  if (error) {
    printDiagnostic(source, filename, context, error);

    return false;
  }

  return true;
}

bool EdonRuntime::executeFile(const std::string &filepath) {
  std::ifstream file(filepath);

  if (!file.is_open()) {
    std::cerr << "edon error: Cannot open file " << filepath << '\n';

    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  return executeSource(buffer.str(), filepath);
}
