#pragma once
#include <JavaScriptCore/JavaScript.h>
#include <string>

class EdonRuntime {
private:
    JSGlobalContextRef context;

    void bootstrapCoreLibraries();

public:
    EdonRuntime();
    ~EdonRuntime();

    bool executeFile(const std::string& filepath);
    bool executeSource(const std::string& source, const std::string& filename = "<anonymous>");
};
