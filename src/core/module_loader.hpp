#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <functional>
#include <string>
#include <unordered_map>

class ModuleLoader {
public:
  using RequireFactory = std::function<JSObjectRef(const std::string &parentFilename)>;

  ModuleLoader(JSGlobalContextRef context, RequireFactory makeRequire);
  ~ModuleLoader();

  ModuleLoader(const ModuleLoader &) = delete;
  ModuleLoader &operator=(const ModuleLoader &) = delete;

  JSValueRef require(const std::string &name, const std::string &parentFilename, JSValueRef *error);
  JSValueRef loadFile(const std::string &filepath, JSValueRef *error);

  static void setError(JSContextRef context, JSValueRef *error, const char *message);

private:
  JSGlobalContextRef context;
  RequireFactory makeRequire;
  std::unordered_map<std::string, JSObjectRef> cache;
};
