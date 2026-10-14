#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <memory>
#include <string>

class ModuleLoader;

class EdonRuntime {
private:
  struct RequireData;

  JSGlobalContextRef context = nullptr;
  std::unique_ptr<ModuleLoader> moduleLoader;

  static JSClassRef getRequireClass();
  static void finalizeRequire(JSObjectRef object);

  static JSValueRef requireCallAsFunction(JSContextRef context, JSObjectRef function,
                                          JSObjectRef thisObject, std::size_t argumentCount,
                                          const JSValueRef arguments[], JSValueRef *error);

  JSObjectRef makeRequire(const std::string &parentFilename);
  void bootstrapCoreLibraries();

public:
  EdonRuntime();
  ~EdonRuntime();

  EdonRuntime(const EdonRuntime &) = delete;
  EdonRuntime &operator=(const EdonRuntime &) = delete;

  bool executeFile(const std::string &filepath);
  bool executeSource(const std::string &source, const std::string &filename = "<anonymous>");
};
