#pragma once
#include <JavaScriptCore/JavaScript.h>
#include <string>

namespace edon {
    // Registers raw I/O bindings (__edon_write, __edon_isatty) into JavaScriptCore global scope
    void registerNativeIO(JSGlobalContextRef ctx);

    // C++ system safety initializers (e.g. SIGPIPE handling for unix pipes)
    void initSystemIO();
}
