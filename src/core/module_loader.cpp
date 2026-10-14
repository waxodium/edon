#include "module_loader.hpp"

#include "ffi/api.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace {

JSStringRef makeJSString(const std::string &value) {
  return JSStringCreateWithUTF8CString(value.c_str());
}

void setProperty(JSContextRef context, JSObjectRef object, const char *name, JSValueRef value,
                 JSValueRef *error = nullptr) {
  JSStringRef key = JSStringCreateWithUTF8CString(name);
  if (!key) return;

  JSObjectSetProperty(context, object, key, value, kJSPropertyAttributeNone, error);
  JSStringRelease(key);
}

JSValueRef getProperty(JSContextRef context, JSObjectRef object, const char *name,
                       JSValueRef *error = nullptr) {
  JSStringRef key = JSStringCreateWithUTF8CString(name);
  if (!key) return JSValueMakeUndefined(context);

  JSValueRef value = JSObjectGetProperty(context, object, key, error);
  JSStringRelease(key);
  return value;
}

void markCommonJSWrapperError(JSContextRef context, JSValueRef error, const std::string &filepath) {
  if (!context || !error || !JSValueIsObject(context, error) || filepath.empty()) return;

  JSObjectRef object = JSValueToObject(context, error, nullptr);
  if (!object) return;

  JSValueRef propertyError = nullptr;
  JSValueRef value = getProperty(context, object, "edonCommonJSWrapperFiles", &propertyError);
  JSObjectRef files = nullptr;

  if (!propertyError && value && JSValueIsObject(context, value)) {
    files = JSValueToObject(context, value, nullptr);
    if (files && !JSValueIsArray(context, value)) files = nullptr;
  }

  if (!files) {
    files = JSObjectMakeArray(context, 0, nullptr, nullptr);
    if (!files) return;
    setProperty(context, object, "edonCommonJSWrapperFiles", files);
  }

  JSStringRef lengthKey = JSStringCreateWithUTF8CString("length");
  if (!lengthKey) return;

  JSValueRef lengthValue = JSObjectGetProperty(context, files, lengthKey, nullptr);
  JSStringRelease(lengthKey);
  const double rawLength = lengthValue ? JSValueToNumber(context, lengthValue, nullptr) : 0;
  const std::size_t length = rawLength > 0 ? static_cast<std::size_t>(rawLength) : 0;

  for (std::size_t i = 0; i < length; ++i) {
    JSValueRef existing =
        JSObjectGetPropertyAtIndex(context, files, static_cast<unsigned>(i), nullptr);
    JSStringRef text = JSValueToStringCopy(context, existing, nullptr);
    if (!text) continue;

    const std::size_t capacity = JSStringGetMaximumUTF8CStringSize(text);
    std::string existingPath(capacity, '\0');
    const std::size_t size = JSStringGetUTF8CString(text, existingPath.data(), capacity);
    JSStringRelease(text);

    if (size > 0) existingPath.resize(size - 1);
    if (existingPath == filepath) return;
  }

  JSStringRef pathString = makeJSString(filepath);
  if (!pathString) return;

  JSValueRef pathValue = JSValueMakeString(context, pathString);
  JSStringRelease(pathString);
  JSObjectSetPropertyAtIndex(context, files, static_cast<unsigned>(length), pathValue, nullptr);
}

} // namespace

ModuleLoader::ModuleLoader(JSGlobalContextRef context, RequireFactory makeRequire)
    : context(context),
      makeRequire(std::move(makeRequire)) {}

ModuleLoader::~ModuleLoader() {
  for (const auto &[path, module] : cache) {
    (void)path;
    JSValueUnprotect(context, module);
  }
}

void ModuleLoader::setError(JSContextRef context, JSValueRef *error, const char *message) {
  if (!error) return;

  JSStringRef text = JSStringCreateWithUTF8CString(message);
  if (!text) return;

  JSValueRef argument = JSValueMakeString(context, text);
  *error = JSObjectMakeError(context, 1, &argument, nullptr);
  JSStringRelease(text);
}

JSValueRef ModuleLoader::require(const std::string &name, const std::string &parentFilename,
                                 JSValueRef *error) {
  if (name == "edon:ffi") return edon::ffi::requireModule(context, name, error);

  const bool isJS = name.size() >= 3 && name.compare(name.size() - 3, 3, ".js") == 0;
  const bool isCJS = name.size() >= 4 && name.compare(name.size() - 4, 4, ".cjs") == 0;

  if (!isJS && !isCJS) {
    const std::string message = "Cannot find module '" + name + "'";
    setError(context, error, message.c_str());
    return JSValueMakeUndefined(context);
  }

  namespace fs = std::filesystem;
  try {
    fs::path requested(name);

    if (!requested.is_absolute()) {
      const fs::path base =
          parentFilename.empty() ? fs::current_path() : fs::path(parentFilename).parent_path();
      requested = base / requested;
    }

    return loadFile(requested.string(), error);
  } catch (const fs::filesystem_error &) {
    setError(context, error, "Could not resolve the requested module path");
    return JSValueMakeUndefined(context);
  }
}

JSValueRef ModuleLoader::loadFile(const std::string &filepath, JSValueRef *error) {
  namespace fs = std::filesystem;

  std::string resolved;
  try {
    resolved = fs::weakly_canonical(filepath).string();
  } catch (const fs::filesystem_error &) {
    setError(context, error, "Could not resolve module path");
    return JSValueMakeUndefined(context);
  }

  if (const auto found = cache.find(resolved); found != cache.end())
    return getProperty(context, found->second, "exports", error);

  std::ifstream file(resolved);
  if (!file.is_open()) {
    const std::string message = "Cannot open module: " + resolved;
    setError(context, error, message.c_str());
    return JSValueMakeUndefined(context);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();

  if (file.bad()) {
    const std::string message = "Failed to read module: " + resolved;
    setError(context, error, message.c_str());
    return JSValueMakeUndefined(context);
  }

  const std::string source = buffer.str();
  JSObjectRef module = JSObjectMake(context, nullptr, nullptr);
  JSObjectRef exports = JSObjectMake(context, nullptr, nullptr);

  if (!module || !exports) {
    setError(context, error, "Failed to create module objects");
    return JSValueMakeUndefined(context);
  }

  setProperty(context, module, "exports", exports, error);
  if (error && *error) return JSValueMakeUndefined(context);

  const auto [cacheEntry, inserted] = cache.emplace(resolved, module);
  if (!inserted) return getProperty(context, cacheEntry->second, "exports", error);

  JSValueProtect(context, module);
  JSObjectRef localRequire = makeRequire(resolved);

  if (!localRequire) {
    cache.erase(resolved);
    JSValueUnprotect(context, module);
    setError(context, error, "Failed to create require function");
    return JSValueMakeUndefined(context);
  }

  const std::string directory = fs::path(resolved).parent_path().string();
  JSStringRef filenameString = makeJSString(resolved);
  JSStringRef dirnameString = makeJSString(directory);
  JSStringRef script = nullptr;
  JSStringRef scriptName = nullptr;
  JSValueRef localError = nullptr;

  if (filenameString && dirnameString) {
    JSValueRef filenameValue = JSValueMakeString(context, filenameString);
    JSValueRef dirnameValue = JSValueMakeString(context, dirnameString);
    const std::string wrapped =
        "(function(exports, require, module, __filename, __dirname) {\n" + source + "\n})";

    script = makeJSString(wrapped);
    scriptName = makeJSString(resolved);

    if (script && scriptName) {
      JSValueRef functionValue =
          JSEvaluateScript(context, script, nullptr, scriptName, 1, &localError);

      if (!localError && functionValue && JSValueIsObject(context, functionValue)) {
        JSObjectRef function = JSValueToObject(context, functionValue, &localError);

        if (function && JSObjectIsFunction(context, function)) {
          JSValueRef arguments[] = {exports, localRequire, module, filenameValue, dirnameValue};
          JSObjectCallAsFunction(context, function, nullptr, 5, arguments, &localError);
        } else if (!localError) {
          setError(context, &localError, "Failed to compile CommonJS module");
        }
      } else if (!localError) {
        setError(context, &localError, "Failed to compile CommonJS module");
      }
    } else {
      setError(context, &localError, "Failed to create module source strings");
    }
  } else {
    setError(context, &localError, "Failed to create module source strings");
  }

  if (script) JSStringRelease(script);
  if (scriptName) JSStringRelease(scriptName);
  if (filenameString) JSStringRelease(filenameString);
  if (dirnameString) JSStringRelease(dirnameString);

  if (localError) {
    markCommonJSWrapperError(context, localError, resolved);
    cache.erase(resolved);
    JSValueUnprotect(context, module);
    if (error) *error = localError;
    return JSValueMakeUndefined(context);
  }

  return getProperty(context, module, "exports", error);
}
