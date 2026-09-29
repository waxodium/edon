#include "ffi.hpp"

#include "ffi_call.hpp"
#include "ffi_callback.hpp"
#include "ffi_errors.hpp"
#include "ffi_memory.hpp"
#include "ffi_native.hpp"
#include "ffi_types.hpp"
#include "runtime_output.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>

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

std::string toString(JSContextRef context, JSValueRef value, JSValueRef *error) {
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
  const size_t actualSize = JSStringGetUTF8CString(string, result.data(), maxSize);
  JSStringRelease(string);

  if (actualSize == 0)
    return {};

  result.resize(actualSize - 1);
  return result;
}

JSValueRef getProperty(JSContextRef context, JSObjectRef object, const char *name, JSValueRef *error) {
  if (!object || !name)
    return nullptr;

  JSStringRef property = JSStringCreateWithUTF8CString(name);
  if (!property) {
    throwError(context, error, ErrorCode::PropertyCreationFailed);
    return nullptr;
  }

  JSValueRef value = JSObjectGetProperty(context, object, property, error);
  JSStringRelease(property);
  return value;
}

bool getSignature(JSContextRef context, JSValueRef value, Signature &signature, JSValueRef *error) {
  if (!JSValueIsObject(context, value)) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return false;
  }

  JSObjectRef object = JSValueToObject(context, value, error);
  if (!object)
    return false;

  JSValueRef returnsValue = getProperty(context, object, "returns", error);
  if (error && *error)
    return false;

  if (!returnsValue || JSValueIsUndefined(context, returnsValue)) {
    throwError(context, error, ErrorCode::MissingReturnType);
    return false;
  }

  std::string returnName = toString(context, returnsValue, error);
  if (error && *error)
    return false;

  if (returnName.empty()) {
    throwError(context, error, ErrorCode::EmptyReturnType);
    return false;
  }

  std::shared_ptr<Type> returnType = parseTypeName(returnName);
  if (!returnType) {
    throwError(context, error, ErrorCode::UnknownReturnType, returnName);
    return false;
  }

  JSValueRef argsValue = getProperty(context, object, "args", error);
  if (error && *error)
    return false;

  if (!argsValue || !JSValueIsObject(context, argsValue)) {
    throwError(context, error, ErrorCode::MissingArgumentTypes);
    return false;
  }

  JSObjectRef argsObject = JSValueToObject(context, argsValue, error);
  if (!argsObject)
    return false;

  JSValueRef lengthValue = getProperty(context, argsObject, "length", error);
  if (error && *error)
    return false;

  if (!lengthValue) {
    throwError(context, error, ErrorCode::InvalidArgumentList);
    return false;
  }

  double lengthNumber = JSValueToNumber(context, lengthValue, error);
  if (error && *error)
    return false;

  if (!std::isfinite(lengthNumber) || lengthNumber < 0.0 ||
      std::floor(lengthNumber) != lengthNumber ||
      lengthNumber > static_cast<double>(std::numeric_limits<size_t>::max())) {
    throwError(context, error, ErrorCode::InvalidArgumentList);
    return false;
  }

  const size_t count = static_cast<size_t>(lengthNumber);
  Signature parsedSignature;

  parsedSignature.returns = std::move(returnType);
  parsedSignature.args.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    JSStringRef property = JSStringCreateWithUTF8CString(std::to_string(i).c_str());
    if (!property) {
      throwError(context, error, ErrorCode::PropertyCreationFailed);
      return false;
    }

    JSValueRef argValue = JSObjectGetProperty(context, argsObject, property, error);
    JSStringRelease(property);

    if (error && *error)
      return false;

    if (!argValue || JSValueIsUndefined(context, argValue)) {
      throwError(context, error, ErrorCode::MissingArgumentType, i);
      return false;
    }

    std::string argName = toString(context, argValue, error);
    if (error && *error)
      return false;

    if (argName.empty()) {
      throwError(context, error, ErrorCode::EmptyArgumentType, i);
      return false;
    }

    std::shared_ptr<Type> type = parseTypeName(argName);
    if (!type) {
      throwError(context, error, ErrorCode::UnknownArgumentType, argName);
      return false;
    }

    parsedSignature.args.push_back(std::move(type));
  }

  if (!prepareSignature(parsedSignature)) {
    throwError(context, error, ErrorCode::SignaturePreparationFailed);
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
  auto *holder = static_cast<NativeFunctionHolder *>(JSObjectGetPrivate(object));
  delete holder;
}

JSValueRef nativeFunctionCall(JSContextRef context, JSObjectRef function,
                              JSObjectRef, size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  auto *holder = static_cast<NativeFunctionHolder *>(JSObjectGetPrivate(function));

  if (!holder || !holder->function || !holder->function->address) {
    throwError(context, error, ErrorCode::InvalidNativeFunction);
    return JSValueMakeUndefined(context);
  }

  return callNativeFunction(context, *holder->function, holder->signature,
                            argumentCount, arguments, error);
}

JSValueRef moduleCFunction(JSContextRef context, JSObjectRef,
                            JSObjectRef thisObject, size_t argumentCount,
                            const JSValueRef arguments[], JSValueRef *error) {
  if (argumentCount != 2) {
    throwError(context, error, ErrorCode::ArgumentCountMismatch, 2, argumentCount);
    return JSValueMakeUndefined(context);
  }

  auto *moduleHolder = static_cast<NativeModuleHolder *>(JSObjectGetPrivate(thisObject));

  if (!moduleHolder || !moduleHolder->state) {
    throwError(context, error, ErrorCode::InvalidNativeModule);
    return JSValueMakeUndefined(context);
  }

  std::string name = toString(context, arguments[0], error);
  if (error && *error)
    return JSValueMakeUndefined(context);

  if (name.empty()) {
    throwError(context, error, ErrorCode::EmptyFunctionName);
    return JSValueMakeUndefined(context);
  }

  Signature signature;
  if (!getSignature(context, arguments[1], signature, error)) {
    return JSValueMakeUndefined(context);
  }

  void *address = moduleHolder->state->resolve(name);
  if (!address) {
    throwError(context, error, ErrorCode::FunctionNotFound, name);
    return JSValueMakeUndefined(context);
  }

  auto functionState = std::make_shared<NativeFunctionState>(moduleHolder->state, address, name);
  if (!functionState) {
    throwError(context, error, ErrorCode::FunctionCreationFailed);
    return JSValueMakeUndefined(context);
  }

  auto *functionHolder = new NativeFunctionHolder();
  functionHolder->function = std::move(functionState);
  functionHolder->signature = std::move(signature);

  JSObjectRef function = JSObjectMake(context, nativeFunctionClass, functionHolder);
  if (!function) {
    delete functionHolder;
    throwError(context, error, ErrorCode::FunctionCreationFailed);
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

  JSObjectRef function = JSObjectMakeFunctionWithCallback(context, functionName, callback);
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

JSValueRef createCallbackFunction(JSContextRef context, JSObjectRef,
                                  JSObjectRef, size_t argumentCount,
                                  const JSValueRef arguments[],
                                  JSValueRef *error) {
  if (argumentCount != 2) {
    throwError(context, error, ErrorCode::ArgumentCountMismatch, 2, argumentCount);
    return JSValueMakeUndefined(context);
  }

  if (!JSValueIsObject(context, arguments[0])) {
    throwError(context, error, ErrorCode::ExpectedFunction);
    return JSValueMakeUndefined(context);
  }

  JSObjectRef function = JSValueToObject(context, arguments[0], error);
  if (!function)
    return JSValueMakeUndefined(context);

  if (!JSObjectIsFunction(context, function)) {
    throwError(context, error, ErrorCode::ExpectedFunction);
    return JSValueMakeUndefined(context);
  }

  Signature signature;
  if (!getSignature(context, arguments[1], signature, error)) {
    return JSValueMakeUndefined(context);
  }

  JSObjectRef callback = createCallback(context, function, std::move(signature), error);
  if (!callback)
    return JSValueMakeUndefined(context);

  return callback;
}

JSValueRef destroyCallbackFunction(JSContextRef context, JSObjectRef,
                                    JSObjectRef, size_t argumentCount,
                                    const JSValueRef arguments[],
                                    JSValueRef *error) {
  if (argumentCount != 1) {
    throwError(context, error, ErrorCode::ArgumentCountMismatch, 1, argumentCount);
    return JSValueMakeUndefined(context);
  }

  return destroyCallback(context, arguments[0], error);
}

JSValueRef jsC(JSContextRef context, JSObjectRef, JSObjectRef,
                size_t argumentCount, const JSValueRef arguments[],
                JSValueRef *error) {
  if (argumentCount == 0) {
    throwError(context, error, ErrorCode::MissingSource);
    return JSValueMakeUndefined(context);
  }

  std::string source;

  if (JSValueIsString(context, arguments[0])) {
    source = toString(context, arguments[0], error);
    if (error && *error)
      return JSValueMakeUndefined(context);
  } else if (JSValueIsObject(context, arguments[0])) {
    if (argumentCount != 1) {
      throwError(context, error, ErrorCode::TemplateSubstitution);
      return JSValueMakeUndefined(context);
    }

    JSObjectRef strings = JSValueToObject(context, arguments[0], error);
    if (!strings)
      return JSValueMakeUndefined(context);

    JSValueRef first = getProperty(context, strings, "0", error);
    if (error && *error)
      return JSValueMakeUndefined(context);

    if (!first) {
      throwError(context, error, ErrorCode::MissingSource);
      return JSValueMakeUndefined(context);
    }

    source = toString(context, first, error);
    if (error && *error)
      return JSValueMakeUndefined(context);
  } else {
    throwError(context, error, ErrorCode::InvalidSource);
    return JSValueMakeUndefined(context);
  }

  if (source.empty()) {
    throwError(context, error, ErrorCode::EmptySource);
    return JSValueMakeUndefined(context);
  }

  std::string compileError;
  NativeCompileDiagnostic diagnostic;

  std::shared_ptr<NativeModuleState> module =
      NativeModuleState::compile(source, compileError, diagnostic);

  if (!module) {
    throwError(context, error, ErrorCode::CompilationFailed);

    if (error && *error && !diagnostic.message.empty()) {
      JSObjectRef errorObject = JSValueToObject(context, *error, nullptr);

      if (errorObject) {
        JSStringRef compilerErrorProperty = JSStringCreateWithUTF8CString("compilerError");
        JSStringRef compilerSourceProperty = JSStringCreateWithUTF8CString("compilerSource");
        JSStringRef compilerErrorValue = JSStringCreateWithUTF8CString(diagnostic.message.c_str());
        JSStringRef compilerSourceValue = JSStringCreateWithUTF8CString(diagnostic.source.c_str());

        if (compilerErrorProperty && compilerErrorValue) {
          JSObjectSetProperty(context, errorObject, compilerErrorProperty,
                              JSValueMakeString(context, compilerErrorValue),
                              kJSPropertyAttributeNone, nullptr);
        }

        if (compilerSourceProperty && compilerSourceValue) {
          JSObjectSetProperty(context, errorObject, compilerSourceProperty,
                              JSValueMakeString(context, compilerSourceValue),
                              kJSPropertyAttributeNone, nullptr);
        }

        if (compilerErrorValue)
          JSStringRelease(compilerErrorValue);
        if (compilerSourceValue)
          JSStringRelease(compilerSourceValue);
        if (compilerErrorProperty)
          JSStringRelease(compilerErrorProperty);
        if (compilerSourceProperty)
          JSStringRelease(compilerSourceProperty);
      }
    }

    return JSValueMakeUndefined(context);
  }

  auto *holder = new NativeModuleHolder();
  holder->state = std::move(module);

  JSObjectRef object = JSObjectMake(context, nativeModuleClass, holder);
  if (!object) {
    delete holder;
    throwError(context, error, ErrorCode::ObjectCreationFailed);
    return JSValueMakeUndefined(context);
  }

  attachModuleProperties(context, object);
  return object;
}

JSValueRef jsLoadNativeModule(JSContextRef context, JSObjectRef, JSObjectRef,
                              size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  if (argumentCount != 1 || !JSValueIsString(context, arguments[0])) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return JSValueMakeUndefined(context);
  }

  std::string path = toString(context, arguments[0], error);
  if (error && *error)
    return JSValueMakeUndefined(context);

  if (path.empty()) {
    throwError(context, error, ErrorCode::EmptyModulePath);
    return JSValueMakeUndefined(context);
  }

  std::string loadError;
  std::shared_ptr<NativeModuleState> module = NativeModuleState::load(path, loadError);

  if (!module) {
    if (!loadError.empty()) {
      throwError(context, error, ErrorCode::ModuleLoadFailed, loadError);
    } else {
      throwError(context, error, ErrorCode::ModuleLoadFailed, path);
    }

    return JSValueMakeUndefined(context);
  }

  auto *holder = new NativeModuleHolder();
  holder->state = std::move(module);

  JSObjectRef object = JSObjectMake(context, nativeModuleClass, holder);
  if (!object) {
    delete holder;
    throwError(context, error, ErrorCode::ObjectCreationFailed);
    return JSValueMakeUndefined(context);
  }

  attachModuleProperties(context, object);
  return object;
}

void attachFFIExports(JSContextRef context, JSObjectRef module) {
  setFunctionProperty(context, module, "c", jsC);
  setFunctionProperty(context, module, "loadLibrary", jsLoadNativeModule);
  setFunctionProperty(context, module, "allocateSharedBuffer", allocateSharedBufferFunction);
  setFunctionProperty(context, module, "addressOf", addressOfFunction);
  setFunctionProperty(context, module, "free", freeNativeMemoryFunction);
  setFunctionProperty(context, module, "createCallback", createCallbackFunction);
  setFunctionProperty(context, module, "destroyCallback", destroyCallbackFunction);
}

void attachModuleProperties(JSContextRef context, JSObjectRef module) {
  attachFFIExports(context, module);
  setFunctionProperty(context, module, "cfunction", moduleCFunction);
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
    throwError(context, error, ErrorCode::ModuleNotFound, name);
    return JSValueMakeUndefined(context);
  }

  if (!nativeModuleClass) {
    throwError(context, error, ErrorCode::ModuleSystemUnavailable);
    return JSValueMakeUndefined(context);
  }

  JSObjectRef module = JSObjectMake(context, nullptr, nullptr);
  if (!module) {
    throwError(context, error, ErrorCode::ObjectCreationFailed);
    return JSValueMakeUndefined(context);
  }

  attachFFIExports(context, module);
  return module;
}

} // namespace ffi
} // namespace edon
