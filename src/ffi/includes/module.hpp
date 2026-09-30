#pragma once

#include <JavaScriptCore/JavaScript.h>

namespace edon {
namespace ffi {

void initializeModuleClasses();

JSValueRef requireFFIModule(
    JSContextRef context,
    JSValueRef *error);

} // namespace ffi
} // namespace edon
