#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <string>

namespace edon {
namespace ffi {

void registerNamespace(JSGlobalContextRef context);
JSValueRef requireModule(JSContextRef context, const std::string &name, JSValueRef *error);

} // namespace ffi
} // namespace edon
