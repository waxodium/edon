#pragma once

#include "module_loader.hpp"
#include "runtime.hpp"

#include <string>

struct EdonRuntime::RequireData {
  EdonRuntime *runtime;
  std::string parentFilename;
};

inline JSClassRef EdonRuntime::getRequireClass() {
  static JSClassRef cls = [] {
    JSClassDefinition definition = kJSClassDefinitionEmpty;

    definition.className = "EdonRequire";
    definition.finalize = &EdonRuntime::finalizeRequire;
    definition.callAsFunction = &EdonRuntime::requireCallAsFunction;

    return JSClassCreate(&definition);
  }();

  return cls;
}

inline void EdonRuntime::finalizeRequire(JSObjectRef object) {
  delete static_cast<RequireData *>(JSObjectGetPrivate(object));
}

inline JSValueRef EdonRuntime::requireCallAsFunction(JSContextRef context, JSObjectRef function,
                                                     JSObjectRef, std::size_t argumentCount,
                                                     const JSValueRef arguments[],
                                                     JSValueRef *error) {
  auto *data = static_cast<RequireData *>(JSObjectGetPrivate(function));

  if (!data || argumentCount != 1 || !JSValueIsString(context, arguments[0])) {
    ModuleLoader::setError(context, error, "require() requires exactly one module name string");
    return JSValueMakeUndefined(context);
  }

  JSStringRef text = JSValueToStringCopy(context, arguments[0], error);
  if (!text) return JSValueMakeUndefined(context);

  const std::size_t capacity = JSStringGetMaximumUTF8CStringSize(text);

  std::string name(capacity, '\0');
  const std::size_t size = JSStringGetUTF8CString(text, name.data(), capacity);

  JSStringRelease(text);

  if (size == 0) {
    ModuleLoader::setError(context, error, "require() received an invalid module name");
    return JSValueMakeUndefined(context);
  }

  name.resize(size - 1);

  return data->runtime->moduleLoader->require(name, data->parentFilename, error);
}

inline JSObjectRef EdonRuntime::makeRequire(const std::string &parentFilename) {
  auto *data = new RequireData{this, parentFilename};

  JSObjectRef function = JSObjectMake(context, getRequireClass(), data);

  if (!function) delete data;

  return function;
}
