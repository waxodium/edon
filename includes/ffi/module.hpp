#pragma once

#include "function.hpp"
#include "signature.hpp"
#include "types.hpp"

#include <JavaScriptCore/JavaScript.h>

namespace edon { namespace ffi {

void initializeModuleClasses();

JSObjectRef makeNativeFunction(JSContextRef context, void *address, Signature signature,
                               JSValueRef *error);

NativeFunctionState *getNativeFunctionState(JSContextRef context, JSValueRef value);

JSValueRef requireFFIModule(JSContextRef context, JSValueRef *error);

}} // namespace edon::ffi
