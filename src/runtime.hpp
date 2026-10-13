#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <string>
#include <unordered_map>

class EdonRuntime {
private:
  struct RequireData;

  JSGlobalContextRef context = nullptr;
  std::unordered_map<std::string, JSObjectRef> moduleCache;

  static JSClassRef getRequireClass();
  static void finalizeRequire(JSObjectRef object);

  static JSValueRef requireCallAsFunction(JSContextRef context, JSObjectRef function,
                                          JSObjectRef thisObject, std::size_t argumentCount,
                                          const JSValueRef arguments[], JSValueRef *error);

  JSObjectRef makeRequire(const std::string &parentFilename);

  JSValueRef requireModule(const std::string &name, const std::string &parentFilename,
                           JSValueRef *error);

  JSValueRef loadCommonJSFile(const std::string &filepath, JSValueRef *error);

  void bootstrapCoreLibraries();

public:
  EdonRuntime();
  ~EdonRuntime();

  EdonRuntime(const EdonRuntime &) = delete;
  EdonRuntime &operator=(const EdonRuntime &) = delete;

  bool executeFile(const std::string &filepath);

  bool executeSource(const std::string &source, const std::string &filename = "<anonymous>");
};
