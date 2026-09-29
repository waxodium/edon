#pragma once
#include <JavaScriptCore/JavaScript.h>
#include <string>

namespace edon {
    void registerNativeIO(JSGlobalContextRef context);
    void initSystemIO();
}
