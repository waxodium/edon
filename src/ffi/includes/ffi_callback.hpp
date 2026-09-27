#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <memory>

#include "ffi_types.hpp"

namespace edon {
namespace ffi {

struct CallbackState {
    JSGlobalContextRef context = nullptr;
    JSObjectRef function = nullptr;
    Signature signature;
    void* closure = nullptr;
    void* executable = nullptr;
    bool alive = false;
    bool destroyed = false;

    CallbackState() = default;
    ~CallbackState();

    CallbackState(const CallbackState&) = delete;
    CallbackState& operator=(const CallbackState&) = delete;

    void destroy();
};

JSObjectRef createCallback(JSContextRef context, JSObjectRef function, Signature signature, JSValueRef* exception);

JSValueRef destroyCallback(JSContextRef context, JSValueRef callback, JSValueRef* exception);

CallbackState* getCallbackState(JSContextRef context, JSValueRef value);

} // namespace ffi
} // namespace edon
