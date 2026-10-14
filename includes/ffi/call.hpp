#pragma once

#include "function.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <memory>

namespace edon { namespace ffi {

bool jsValueToNative(JSContextRef context, JSValueRef value, const std::shared_ptr<Type> &type,
                     void *destination, JSValueRef *error);

JSValueRef nativeToJSValue(JSContextRef context, const std::shared_ptr<Type> &type,
                           const void *value, JSValueRef *error);

JSValueRef callNativeFunction(JSContextRef context, const NativeFunctionState &function,
                              const Signature &signature, std::size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error);

}} // namespace edon::ffi
