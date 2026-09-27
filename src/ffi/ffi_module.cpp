#include "ffi.hpp"

#include "ffi_call.hpp"
#include "ffi_callback.hpp"
#include "ffi_memory.hpp"
#include "ffi_native.hpp"
#include "ffi_types.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>

namespace edon {
namespace ffi {

namespace {

struct NativeModuleHolder {
  std::shared_ptr<NativeModuleState> state;
};

struct NativeFunctionHolder {
  std::shared_ptr<NativeFunctionState> function;
  Signature signature;
};

JSClassRef nativeModuleClass = nullptr;
JSClassRef nativeFunctionClass = nullptr;

void attachFFIExports(JSContextRef context, JSObjectRef module);
void attachModuleProperties(JSContextRef context, JSObjectRef module);

JSValueRef makeError(JSContextRef context, const std::string &message) {
  JSStringRef text = JSStringCreateWithUTF8CString(message.c_str());
  if (!text)
    return JSValueMakeUndefined(context);

  JSValueRef args[] = {JSValueMakeString(context, text)};
  JSObjectRef error = JSObjectMakeError(context, 1, args, nullptr);

  JSStringRelease(text);

  if (!error)
    return JSValueMakeUndefined(context);
  return error;
}

void throwError(JSContextRef context, JSValueRef *error,
                const std::string &message) {
  if (!error)
    return;
  *error = makeError(context, message);
}

std::string toString(JSContextRef context, JSValueRef value,
                     JSValueRef *error) {
  if (!value)
    return {};

  JSStringRef string = JSValueToStringCopy(context, value, error);
  if (!string)
    return {};

  const size_t maxSize = JSStringGetMaximumUTF8CStringSize(string);
  if (maxSize == 0) {
    JSStringRelease(string);
    return {};
  }

  std::string result(maxSize, '\0');

  const size_t actualSize =
      JSStringGetUTF8CString(string, result.data(), maxSize);

  JSStringRelease(string);

  if (actualSize == 0)
    return {};

  result.resize(actualSize - 1);
  return result;
}

JSValueRef getProperty(JSContextRef context, JSObjectRef object,
                       const char *name, JSValueRef *error) {
  if (!object || !name)
    return nullptr;

  JSStringRef property = JSStringCreateWithUTF8CString(name);
  if (!property) {
    throwError(context, error, "Failed to create property name");
    return nullptr;
  }

  JSValueRef value = JSObjectGetProperty(context, object, property, error);

  JSStringRelease(property);
  return value;
}

bool getSignature(JSContextRef context, JSValueRef value, Signature &signature,
                  JSValueRef *error) {
  if (!JSValueIsObject(context, value)) {
    throwError(context, error, "FFI signature must be an object");
    return false;
  }

  JSObjectRef object = JSValueToObject(context, value, error);
  if (!object)
    return false;

  JSValueRef returnsValue = getProperty(context, object, "returns", error);

  if (error && *error)
    return false;

  if (!returnsValue || JSValueIsUndefined(context, returnsValue)) {
    throwError(context, error, "FFI signature requires 'returns'");
    return false;
  }

  std::string returnName = toString(context, returnsValue, error);

  if (error && *error)
    return false;

  if (returnName.empty()) {
    throwError(context, error, "FFI return type cannot be empty");
    return false;
  }

  std::shared_ptr<Type> returnType = parseTypeName(returnName);

  if (!returnType) {
    throwError(context, error, "Unknown return type: " + returnName);
    return false;
  }

  JSValueRef argsValue = getProperty(context, object, "args", error);

  if (error && *error)
    return false;

  if (!argsValue || !JSValueIsObject(context, argsValue)) {
    throwError(context, error, "FFI signature requires 'args' array");
    return false;
  }

  JSObjectRef argsObject = JSValueToObject(context, argsValue, error);

  if (!argsObject)
    return false;

  JSValueRef lengthValue = getProperty(context, argsObject, "length", error);

  if (error && *error)
    return false;

  if (!lengthValue) {
    throwError(context, error, "Invalid FFI argument list");
    return false;
  }

  double lengthNumber = JSValueToNumber(context, lengthValue, error);

  if (error && *error)
    return false;

  if (!std::isfinite(lengthNumber) || lengthNumber < 0.0 ||
      std::floor(lengthNumber) != lengthNumber ||
      lengthNumber > static_cast<double>(std::numeric_limits<size_t>::max())) {
    throwError(context, error, "Invalid FFI argument list");
    return false;
  }

  const size_t count = static_cast<size_t>(lengthNumber);

  Signature parsedSignature;
  parsedSignature.returns = std::move(returnType);
  parsedSignature.args.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    const std::string index = std::to_string(i);

    JSStringRef property = JSStringCreateWithUTF8CString(index.c_str());

    if (!property) {
      throwError(context, error, "Failed to create FFI argument property");
      return false;
    }

    JSValueRef argValue =
        JSObjectGetProperty(context, argsObject, property, error);

    JSStringRelease(property);

    if (error && *error)
      return false;

    if (!argValue || JSValueIsUndefined(context, argValue)) {
      throwError(context, error, "Missing FFI argument type at index " + index);
      return false;
    }

    std::string argName = toString(context, argValue, error);

    if (error && *error)
      return false;

    if (argName.empty()) {
      throwError(context, error,
                 "FFI argument type cannot be empty at index " + index);
      return false;
    }

    std::shared_ptr<Type> type = parseTypeName(argName);

    if (!type) {
      throwError(context, error, "Unknown argument type: [" + argName + "]");
      return false;
    }

    parsedSignature.args.push_back(std::move(type));
  }

  if (!prepareSignature(parsedSignature)) {
    throwError(context, error, "Failed to prepare FFI signature");
    return false;
  }

  signature = std::move(parsedSignature);
  return true;
}

void nativeModuleFinalize(JSObjectRef object) {
  auto *holder = static_cast<NativeModuleHolder *>(JSObjectGetPrivate(object));

  delete holder;
}

void nativeFunctionFinalize(JSObjectRef object) {
  auto *holder =
      static_cast<NativeFunctionHolder *>(JSObjectGetPrivate(object));

  delete holder;
}

JSValueRef nativeFunctionCall(JSContextRef context, JSObjectRef function,
                              JSObjectRef, size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  auto *holder =
      static_cast<NativeFunctionHolder *>(JSObjectGetPrivate(function));

  if (!holder || !holder->function || !holder->function->address) {
    throwError(context, error, "Invalid native function");
    return JSValueMakeUndefined(context);
  }

  return callNativeFunction(context, *holder->function, holder->signature,
                            argumentCount, arguments, error);
}

JSValueRef moduleSymbol(JSContextRef context, JSObjectRef,
                        JSObjectRef thisObject, size_t argumentCount,
                        const JSValueRef arguments[], JSValueRef *error) {
  if (argumentCount != 2) {
    throwError(context, error, "symbol(name, signature) requires 2 arguments");
    return JSValueMakeUndefined(context);
  }

  auto *moduleHolder =
      static_cast<NativeModuleHolder *>(JSObjectGetPrivate(thisObject));

  if (!moduleHolder || !moduleHolder->state) {
    throwError(context, error, "Invalid native module");
    return JSValueMakeUndefined(context);
  }

  std::string name = toString(context, arguments[0], error);

  if (error && *error) {
    return JSValueMakeUndefined(context);
  }

  if (name.empty()) {
    throwError(context, error, "Native symbol name cannot be empty");
    return JSValueMakeUndefined(context);
  }

  Signature signature;

  if (!getSignature(context, arguments[1], signature, error)) {
    return JSValueMakeUndefined(context);
  }

  void *address = moduleHolder->state->resolve(name);

  if (!address) {
    throwError(context, error, "Native symbol not found: " + name);
    return JSValueMakeUndefined(context);
  }

  auto functionState =
      std::make_shared<NativeFunctionState>(moduleHolder->state, address, name);

  if (!functionState) {
    throwError(context, error, "Failed to create native function state");
    return JSValueMakeUndefined(context);
  }

  auto *functionHolder = new NativeFunctionHolder();

  functionHolder->function = std::move(functionState);

  functionHolder->signature = std::move(signature);

  JSObjectRef function =
      JSObjectMake(context, nativeFunctionClass, functionHolder);

  if (!function) {
    delete functionHolder;

    throwError(context, error, "Failed to create native function");

    return JSValueMakeUndefined(context);
  }

  return function;
}

void setFunctionProperty(JSContextRef context, JSObjectRef object,
                         const char *name,
                         JSObjectCallAsFunctionCallback callback) {
  JSStringRef property = JSStringCreateWithUTF8CString(name);

  if (!property)
    return;

  JSStringRef functionName = JSStringCreateWithUTF8CString(name);

  if (!functionName) {
    JSStringRelease(property);
    return;
  }

  JSObjectRef function =
      JSObjectMakeFunctionWithCallback(context, functionName, callback);

  if (function) {
    JSObjectSetProperty(context, object, property, function,
                        kJSPropertyAttributeNone, nullptr);
  }

  JSStringRelease(functionName);
  JSStringRelease(property);
}

JSValueRef allocateSharedBufferFunction(JSContextRef context, JSObjectRef,
                                        JSObjectRef, size_t argumentCount,
                                        const JSValueRef arguments[],
                                        JSValueRef *error) {
  return allocateSharedBuffer(context, argumentCount, arguments, error);
}

JSValueRef addressOfFunction(JSContextRef context, JSObjectRef, JSObjectRef,
                             size_t argumentCount, const JSValueRef arguments[],
                             JSValueRef *error) {
  return addressOf(context, argumentCount, arguments, error);
}

JSValueRef freeNativeMemoryFunction(JSContextRef context, JSObjectRef,
                                    JSObjectRef, size_t argumentCount,
                                    const JSValueRef arguments[],
                                    JSValueRef *error) {
  return freeNativeMemory(context, argumentCount, arguments, error);
}

JSValueRef destroyCallbackFunction(JSContextRef context, JSObjectRef,
                                   JSObjectRef, size_t argumentCount,
                                   const JSValueRef arguments[],
                                   JSValueRef *error) {
  if (argumentCount != 1) {
    throwError(context, error, "destroyCallback(callback) requires 1 argument");
    return JSValueMakeUndefined(context);
  }

  return destroyCallback(context, arguments[0], error);
}

JSValueRef jsC(JSContextRef context, JSObjectRef, JSObjectRef,
               size_t argumentCount, const JSValueRef arguments[],
               JSValueRef *error) {
  if (argumentCount == 0) {
    throwError(context, error, "c() requires C source");
    return JSValueMakeUndefined(context);
  }

  std::string source;

  if (JSValueIsString(context, arguments[0])) {
    source = toString(context, arguments[0], error);

    if (error && *error) {
      return JSValueMakeUndefined(context);
    }
  } else if (JSValueIsObject(context, arguments[0])) {
    if (argumentCount != 1) {
      throwError(context, error,
                 "c tagged templates do not support substitutions");
      return JSValueMakeUndefined(context);
    }

    JSObjectRef strings = JSValueToObject(context, arguments[0], error);

    if (!strings) {
      return JSValueMakeUndefined(context);
    }

    JSValueRef first = getProperty(context, strings, "0", error);

    if (error && *error) {
      return JSValueMakeUndefined(context);
    }

    if (!first) {
      throwError(context, error, "c tagged template has no source");
      return JSValueMakeUndefined(context);
    }

    source = toString(context, first, error);

    if (error && *error) {
      return JSValueMakeUndefined(context);
    }
  } else {
    throwError(context, error,
               "c() expects a C source string or tagged template");
    return JSValueMakeUndefined(context);
  }

  if (source.empty()) {
    throwError(context, error, "C source cannot be empty");
    return JSValueMakeUndefined(context);
  }

  std::string compileError;

  std::shared_ptr<NativeModuleState> module =
      NativeModuleState::compile(source, compileError);

  if (!module) {
    throwError(context, error,
               compileError.empty() ? "TCC compilation failed" : compileError);
    return JSValueMakeUndefined(context);
  }

  auto *holder = new NativeModuleHolder();

  holder->state = std::move(module);

  JSObjectRef object = JSObjectMake(context, nativeModuleClass, holder);

  if (!object) {
    delete holder;

    throwError(context, error, "Failed to create native module");

    return JSValueMakeUndefined(context);
  }

  attachModuleProperties(context, object);
  return object;
}

JSValueRef jsLoadNativeModule(JSContextRef context, JSObjectRef, JSObjectRef,
                              size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  if (argumentCount != 1 || !JSValueIsString(context, arguments[0])) {
    throwError(context, error, "loadLibrary(path) requires a path string");
    return JSValueMakeUndefined(context);
  }

  std::string path = toString(context, arguments[0], error);

  if (error && *error) {
    return JSValueMakeUndefined(context);
  }

  if (path.empty()) {
    throwError(context, error, "Native module path cannot be empty");
    return JSValueMakeUndefined(context);
  }

  std::string loadError;

  std::shared_ptr<NativeModuleState> module =
      NativeModuleState::load(path, loadError);

  if (!module) {
    throwError(context, error,
               loadError.empty() ? "Failed to load native module" : loadError);
    return JSValueMakeUndefined(context);
  }

  auto *holder = new NativeModuleHolder();

  holder->state = std::move(module);

  JSObjectRef object = JSObjectMake(context, nativeModuleClass, holder);

  if (!object) {
    delete holder;

    throwError(context, error, "Failed to create native module");

    return JSValueMakeUndefined(context);
  }

  attachModuleProperties(context, object);
  return object;
}

void attachFFIExports(JSContextRef context, JSObjectRef module) {
  setFunctionProperty(context, module, "c", jsC);

  setFunctionProperty(context, module, "loadLibrary", jsLoadNativeModule);

  setFunctionProperty(context, module, "allocateSharedBuffer",
                      allocateSharedBufferFunction);

  setFunctionProperty(context, module, "addressOf", addressOfFunction);

  setFunctionProperty(context, module, "free", freeNativeMemoryFunction);

  setFunctionProperty(context, module, "destroyCallback",
                      destroyCallbackFunction);
}

void attachModuleProperties(JSContextRef context, JSObjectRef module) {
  attachFFIExports(context, module);

  setFunctionProperty(context, module, "symbol", moduleSymbol);
}

} // namespace

void registerNamespace(JSGlobalContextRef context) {
  (void)context;

  if (!nativeModuleClass) {
    JSClassDefinition definition = kJSClassDefinitionEmpty;

    definition.className = "EdonNativeModule";

    definition.finalize = nativeModuleFinalize;

    nativeModuleClass = JSClassCreate(&definition);
  }

  if (!nativeFunctionClass) {
    JSClassDefinition definition = kJSClassDefinitionEmpty;

    definition.className = "EdonNativeFunction";

    definition.callAsFunction = nativeFunctionCall;

    definition.finalize = nativeFunctionFinalize;

    nativeFunctionClass = JSClassCreate(&definition);
  }
}

JSValueRef requireModule(JSContextRef context, const std::string &name,
                         JSValueRef *error) {
  if (name != "edon:ffi") {
    throwError(context, error, "Cannot find module '" + name + "'");

    return JSValueMakeUndefined(context);
  }

  if (!nativeModuleClass) {
    throwError(context, error, "FFI module system is not initialized");

    return JSValueMakeUndefined(context);
  }

  JSObjectRef module = JSObjectMake(context, nullptr, nullptr);

  if (!module) {
    throwError(context, error, "Failed to create FFI module");

    return JSValueMakeUndefined(context);
  }

  attachFFIExports(context, module);
  return module;
}

} // namespace ffi
} // namespace edon
