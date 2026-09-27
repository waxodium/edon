#pragma once

#include <JavaScriptCore/JavaScript.h>

#include "ffi_types.hpp"
#include "ffi_native.hpp"

namespace edon {
namespace ffi {

JSValueRef callNativeFunction(JSContextRef context, const NativeFunctionState& function, const Signature& signature, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception);

bool jsValueToNative(JSContextRef context, JSValueRef value, const std::shared_ptr<Type>& type, void* destination, JSValueRef* exception);

JSValueRef nativeToJSValue(JSContextRef context, const std::shared_ptr<Type>& type, const void* value, JSValueRef* exception);

} // namespace ffi
} // namespace edon
