#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <string>

namespace edon {

void printDiagnostic(
    const std::string &source,
    const std::string &filename,
    JSContextRef context,
    JSValueRef error
);

void printCompilerDiagnostic(
    const std::string& source,
    const std::string& diagnostic
);

} // namespace edon
