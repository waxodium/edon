#include "native_io.hpp"
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <vector>

namespace edon {

void initSystemIO() {
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

static JSValueRef js_edon_write(JSContextRef context, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception) {
    if (argumentCount < 2) return JSValueMakeBoolean(context, false);

    int fd = static_cast<int>(JSValueToNumber(context, arguments[0], exception));
    if (exception && *exception) return JSValueMakeBoolean(context, false);

    JSStringRef str = JSValueToStringCopy(context, arguments[1], exception);
    if (!str) return JSValueMakeBoolean(context, false);

    size_t maxBytes = JSStringGetMaximumUTF8CStringSize(str);
    std::vector<char> buffer(maxBytes);
    size_t actualBytes = JSStringGetUTF8CString(str, buffer.data(), maxBytes);
    JSStringRelease(str);

    if (actualBytes > 0) actualBytes--;

    size_t totalWritten = 0;
    while (totalWritten < actualBytes) {
        ssize_t n = ::write(fd, buffer.data() + totalWritten, actualBytes - totalWritten);

        if (n > 0) {
            totalWritten += static_cast<size_t>(n);
            continue;
        }

        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE) return JSValueMakeBoolean(context, false);
            return JSValueMakeBoolean(context, false);
        }
    }

    return JSValueMakeBoolean(context, true);
}

static JSValueRef js_edon_isatty(JSContextRef context, JSObjectRef, JSObjectRef, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception) {
    if (argumentCount < 1) return JSValueMakeBoolean(context, false);
    int fd = static_cast<int>(JSValueToNumber(context, arguments[0], exception));
    return JSValueMakeBoolean(context, ::isatty(fd) == 1);
}

void registerNativeIO(JSGlobalContextRef context) {
    JSObjectRef globalObj = JSContextGetGlobalObject(context);

    auto bindFn = [&](const char* name, JSObjectCallAsFunctionCallback fn) {
        JSStringRef jsName = JSStringCreateWithUTF8CString(name);
        JSObjectRef fnObj = JSObjectMakeFunctionWithCallback(context, jsName, fn);
        JSObjectSetProperty(context, globalObj, jsName, fnObj, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(jsName);
    };

    bindFn("__edon_write", js_edon_write);
    bindFn("__edon_isatty", js_edon_isatty);
}

} // namespace edon
