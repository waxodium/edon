#include "call.hpp"
#include "callback.hpp"
#include "errors.hpp"
#include "memory.hpp"
#include "module.hpp"
#include "signature.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <setjmp.h>
#include <signal.h>
#include <string>
#include <utility>
#include <vector>

namespace edon { namespace ffi {

namespace {

thread_local sigjmp_buf nativeRecoveryEnvironment;
thread_local volatile sig_atomic_t nativeCallActive = 0;
thread_local volatile sig_atomic_t nativeSignal = 0;

struct NativeSignalStack {
  void *memory = nullptr;
  bool installed = false;

  ~NativeSignalStack() {
    if (installed) {
      stack_t disabled = {};
      disabled.ss_flags = SS_DISABLE;
      sigaltstack(&disabled, nullptr);
    }

    std::free(memory);
  }
};

thread_local NativeSignalStack nativeSignalStack;

void nativeSignalHandler(int signalNumber, siginfo_t *, void *) {
  if (nativeCallActive) {
    nativeSignal = signalNumber;
    siglongjmp(nativeRecoveryEnvironment, 1);
  }

  struct sigaction action = {};
  sigemptyset(&action.sa_mask);
  action.sa_handler = SIG_DFL;
  sigaction(signalNumber, &action, nullptr);
  raise(signalNumber);
}

bool installNativeSignalHandlers() {
  static std::once_flag once;
  static bool installed = false;

  std::call_once(once, [] {
    struct sigaction action = {};
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = nativeSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;

    const int signals[] = {SIGSEGV, SIGBUS, SIGFPE};

    for (int sig : signals) {
      if (sigaction(sig, &action, nullptr) != 0) return;
    }

    installed = true;
  });

  return installed;
}

bool installNativeSignalStack() {
  if (nativeSignalStack.installed) return true;

  const std::size_t stackSize = SIGSTKSZ > 65536 ? static_cast<std::size_t>(SIGSTKSZ) : 65536;

  void *memory = std::malloc(stackSize);

  if (!memory) return false;

  stack_t stack = {};
  stack.ss_sp = memory;
  stack.ss_size = stackSize;
  stack.ss_flags = 0;

  if (sigaltstack(&stack, nullptr) != 0) {
    std::free(memory);
    return false;
  }

  nativeSignalStack.memory = memory;
  nativeSignalStack.installed = true;

  return true;
}

int invokeProtectedNativeCall(ffi_cif *cif, void (*function)(void), void *result,
                              void **arguments) {
  if (!installNativeSignalHandlers() || !installNativeSignalStack()) return -1;

  nativeSignal = 0;

  if (sigsetjmp(nativeRecoveryEnvironment, 1) != 0) {
    nativeCallActive = 0;
    return static_cast<int>(nativeSignal);
  }

  nativeCallActive = 1;

  ffi_call(cif, FFI_FN(function), result, arguments);

  nativeCallActive = 0;

  return 0;
}

bool getNumber(JSContextRef context, JSValueRef value, double &number, JSValueRef *error) {
  JSValueRef localError = nullptr;
  JSValueRef *targetError = error ? error : &localError;

  number = JSValueToNumber(context, value, targetError);

  return !*targetError;
}

bool isSafeInteger(double value) {
  return std::isfinite(value) && std::trunc(value) == value && value >= -9007199254740991.0 &&
         value <= 9007199254740991.0;
}

template <typename T>
bool convertNumberToInteger(JSContextRef context, JSValueRef value, T &output, JSValueRef *error) {
  if (!JSValueIsNumber(context, value)) {
    throwError(context, error, ErrorCode::ExpectedNumber);
    return false;
  }

  double number = 0;

  if (!getNumber(context, value, number, error)) return false;

  if (!isSafeInteger(number)) {
    throwError(context, error, ErrorCode::UnsafeInteger);
    return false;
  }

  const long double numericValue = static_cast<long double>(number);
  const long double minimum = static_cast<long double>(std::numeric_limits<T>::lowest());
  const long double maximum = static_cast<long double>(std::numeric_limits<T>::max());

  if (numericValue < minimum || numericValue > maximum) {
    throwError(context, error, ErrorCode::IntegerOutOfRange);
    return false;
  }

  output = static_cast<T>(number);

  return true;
}

bool valueToString(JSContextRef context, JSValueRef value, std::string &output, JSValueRef *error) {
  JSValueRef localError = nullptr;
  JSValueRef *targetError = error ? error : &localError;

  JSStringRef string = JSValueToStringCopy(context, value, targetError);

  if (*targetError) return false;

  if (!string) {
    throwError(context, error, ErrorCode::InvalidValue);
    return false;
  }

  const std::size_t size = JSStringGetMaximumUTF8CStringSize(string);

  if (size == 0) {
    JSStringRelease(string);
    throwError(context, error, ErrorCode::InvalidValue);
    return false;
  }

  std::vector<char> buffer;

  try {
    buffer.resize(size);
  } catch (...) {
    JSStringRelease(string);
    throwError(context, error, ErrorCode::NativeCallFailed);
    return false;
  }

  const std::size_t length = JSStringGetUTF8CString(string, buffer.data(), buffer.size());

  JSStringRelease(string);

  if (length == 0) {
    throwError(context, error, ErrorCode::InvalidValue);
    return false;
  }

  output.assign(buffer.data(), length - 1);

  return true;
}

bool parseUnsigned(const std::string &text, uint64_t maximum, uint64_t &output) {
  if (text.empty()) return false;

  std::size_t index = (text[0] == '+') ? 1 : 0;

  if (index == text.size()) return false;

  uint64_t result = 0;

  for (; index < text.size(); ++index) {
    const char character = text[index];

    if (character < '0' || character > '9') return false;

    const uint64_t digit = static_cast<uint64_t>(character - '0');

    if (result > (maximum - digit) / 10) return false;

    result = result * 10 + digit;
  }

  output = result;

  return true;
}

bool parseSigned(const std::string &text, int64_t minimum, int64_t maximum, int64_t &output) {
  if (text.empty()) return false;

  bool negative = false;
  std::size_t index = 0;

  if (text[0] == '-') {
    negative = true;
    index = 1;
  } else if (text[0] == '+') {
    index = 1;
  }

  if (index == text.size()) return false;

  const uint64_t negativeLimit = static_cast<uint64_t>(-(minimum + 1)) + 1;

  const uint64_t limit = negative ? negativeLimit : static_cast<uint64_t>(maximum);

  uint64_t magnitude = 0;

  if (!parseUnsigned(text.substr(index), limit, magnitude)) return false;

  if (negative) {
    if (magnitude == negativeLimit) {
      output = minimum;
      return true;
    }

    output = -static_cast<int64_t>(magnitude);
    return true;
  }

  output = static_cast<int64_t>(magnitude);

  return true;
}

bool convertBigIntToInt64(JSContextRef context, JSValueRef value, int64_t &output,
                          JSValueRef *error) {
  if (!JSValueIsBigInt(context, value)) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  std::string text;

  if (!valueToString(context, value, text, error)) return false;

  if (!parseSigned(text, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
                   output)) {
    throwError(context, error, ErrorCode::IntegerOutOfRange);
    return false;
  }

  return true;
}

bool convertBigIntToUInt64(JSContextRef context, JSValueRef value, uint64_t &output,
                           JSValueRef *error) {
  if (!JSValueIsBigInt(context, value)) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  std::string text;

  if (!valueToString(context, value, text, error)) return false;

  if (!parseUnsigned(text, std::numeric_limits<uint64_t>::max(), output)) {
    throwError(context, error, ErrorCode::IntegerOutOfRange);
    return false;
  }

  return true;
}

bool convertInt64(JSContextRef context, JSValueRef value, int64_t &output, JSValueRef *error) {
  if (JSValueIsBigInt(context, value)) {
    return convertBigIntToInt64(context, value, output, error);
  }

  return convertNumberToInteger(context, value, output, error);
}

bool convertUInt64(JSContextRef context, JSValueRef value, uint64_t &output, JSValueRef *error) {
  if (JSValueIsBigInt(context, value)) {
    return convertBigIntToUInt64(context, value, output, error);
  }

  return convertNumberToInteger(context, value, output, error);
}

template <typename T>
bool convertNumberToFloating(JSContextRef context, JSValueRef value, T &output, JSValueRef *error) {
  if (!JSValueIsNumber(context, value)) {
    throwError(context, error, ErrorCode::ExpectedNumber);
    return false;
  }

  double number = 0;

  if (!getNumber(context, value, number, error)) return false;

  if (!std::isfinite(number)) {
    throwError(context, error, ErrorCode::InvalidFloat);
    return false;
  }

  output = static_cast<T>(number);

  if (!std::isfinite(static_cast<double>(output))) {
    throwError(context, error, ErrorCode::FloatOverflow);
    return false;
  }

  return true;
}

bool convertFunctionPointerArgument(JSContextRef context, JSValueRef value, void *destination,
                                    JSValueRef *error) {
  if (JSValueIsNull(context, value)) {
    *static_cast<void **>(destination) = nullptr;
    return true;
  }

  if (!JSValueIsObject(context, value)) {
    throwError(context, error, ErrorCode::ExpectedPointer);
    return false;
  }

  if (CallbackState *callback = getCallbackState(context, value)) {
    if (!callback->alive || callback->destroyed || !callback->executable) {
      throwError(context, error, ErrorCode::CallbackAlreadyDestroyed);
      return false;
    }

    *static_cast<void **>(destination) = callback->executable;
    return true;
  }

  if (NativeFunctionState *function = getNativeFunctionState(context, value)) {
    if (!function->address) {
      throwError(context, error, ErrorCode::InvalidNativeFunction);
      return false;
    }

    *static_cast<void **>(destination) = function->address;
    return true;
  }

  throwError(context, error, ErrorCode::ExpectedFunction);
  return false;
}

bool convertPointerArgument(JSContextRef context, JSValueRef value, void *destination,
                            JSValueRef *error) {
  if (JSValueIsNull(context, value)) {
    *static_cast<void **>(destination) = nullptr;
    return true;
  }

  if (JSValueIsObject(context, value)) {
    if (CallbackState *callback = getCallbackState(context, value)) {
      if (!callback->alive || callback->destroyed || !callback->executable) {
        throwError(context, error, ErrorCode::CallbackAlreadyDestroyed);
        return false;
      }

      *static_cast<void **>(destination) = callback->executable;
      return true;
    }

    if (NativeFunctionState *function = getNativeFunctionState(context, value)) {
      if (!function->address) {
        throwError(context, error, ErrorCode::InvalidNativeFunction);
        return false;
      }

      *static_cast<void **>(destination) = function->address;
      return true;
    }

    if (getNativePointer(context, value)) {
      std::uintptr_t address = 0;

      if (!getPointerValue(context, value, address, error)) return false;

      if (address == 0) {
        throwError(context, error, ErrorCode::NullPointer);
        return false;
      }

      *static_cast<void **>(destination) = reinterpret_cast<void *>(address);

      return true;
    }

    void *data = nullptr;
    std::size_t size = 0;

    if (getBufferPointer(context, value, data, size, nullptr)) {
      if (!data) {
        throwError(context, error, ErrorCode::InvalidPointer);
        return false;
      }

      *static_cast<void **>(destination) = data;
      return true;
    }
  }

  throwError(context, error, ErrorCode::ExpectedPointer);
  return false;
}

bool convertCharPointerArgument(JSContextRef context, JSValueRef value, void *destination,
                                std::vector<std::vector<char>> &stringStorage, JSValueRef *error) {
  if (JSValueIsNull(context, value)) {
    *static_cast<const char **>(destination) = nullptr;
    return true;
  }

  if (!JSValueIsString(context, value)) {
    throwError(context, error, ErrorCode::ExpectedString);
    return false;
  }

  JSValueRef localError = nullptr;
  JSValueRef *targetError = error ? error : &localError;

  JSStringRef string = JSValueToStringCopy(context, value, targetError);

  if (*targetError) return false;

  if (!string) {
    throwError(context, error, ErrorCode::InvalidCString);
    return false;
  }

  const std::size_t size = JSStringGetMaximumUTF8CStringSize(string);

  if (size == 0) {
    JSStringRelease(string);
    throwError(context, error, ErrorCode::InvalidCString);
    return false;
  }

  std::vector<char> storage;

  try {
    storage.resize(size);
  } catch (...) {
    JSStringRelease(string);
    throwError(context, error, ErrorCode::NativeCallFailed);
    return false;
  }

  const std::size_t length = JSStringGetUTF8CString(string, storage.data(), storage.size());

  JSStringRelease(string);

  if (length == 0) {
    throwError(context, error, ErrorCode::InvalidCString);
    return false;
  }

  stringStorage.push_back(std::move(storage));

  *static_cast<const char **>(destination) = stringStorage.back().data();

  return true;
}

bool isCharPointer(const std::shared_ptr<Type> &type) {
  return type && type->kind == TypeKind::Pointer && type->element &&
         type->element->kind == TypeKind::Char;
}

bool convertArgument(JSContextRef context, JSValueRef value, const std::shared_ptr<Type> &type,
                     void *destination, JSValueRef *error,
                     std::vector<std::vector<char>> &stringStorage);

bool isJSArray(JSContextRef context, JSValueRef value, JSValueRef *error) {
  if (!JSValueIsObject(context, value)) return false;

  JSValueRef localError = nullptr;
  JSValueRef *targetError = error ? error : &localError;

  JSObjectRef global = JSContextGetGlobalObject(context);

  JSStringRef arrayName = JSStringCreateWithUTF8CString("Array");

  if (!arrayName) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return false;
  }

  JSValueRef arrayConstructor = JSObjectGetProperty(context, global, arrayName, targetError);

  JSStringRelease(arrayName);

  if (*targetError || !arrayConstructor || !JSValueIsObject(context, arrayConstructor)) {
    return false;
  }

  JSStringRef isArrayName = JSStringCreateWithUTF8CString("isArray");

  if (!isArrayName) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return false;
  }

  JSObjectRef arrayConstructorObject = const_cast<JSObjectRef>(arrayConstructor);

  JSValueRef isArrayValue =
      JSObjectGetProperty(context, arrayConstructorObject, isArrayName, targetError);

  JSStringRelease(isArrayName);

  if (*targetError || !isArrayValue || !JSValueIsObject(context, isArrayValue)) { return false; }

  JSObjectRef isArrayFunction = const_cast<JSObjectRef>(isArrayValue);

  JSValueRef result = JSObjectCallAsFunction(context, isArrayFunction, arrayConstructorObject, 1,
                                             &value, targetError);

  if (*targetError || !result) return false;

  return JSValueToBoolean(context, result);
}

bool convertFunctionPointerArgument(JSContextRef context, JSValueRef value, void *destination,
                                    JSValueRef *error);

bool convertArrayArgument(JSContextRef context, JSValueRef value, const std::shared_ptr<Type> &type,
                          void *destination, JSValueRef *error,
                          std::vector<std::vector<char>> &stringStorage) {
  if (!type || type->kind != TypeKind::Array || !type->element || type->count == 0) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  if (!JSValueIsObject(context, value) || !isJSArray(context, value, error)) {
    if (error && *error) return false;

    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  JSObjectRef array = const_cast<JSObjectRef>(value);
  const std::size_t elementSize = type->element->size;

  if (elementSize == 0) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return false;
  }

  JSValueRef lengthValue =
      JSObjectGetProperty(context, array, JSStringCreateWithUTF8CString("length"), error);

  if (!lengthValue || (error && *error)) return false;

  double lengthNumber = 0;

  if (!getNumber(context, lengthValue, lengthNumber, error)) return false;

  if (!isSafeInteger(lengthNumber) || lengthNumber < 0 ||
      static_cast<std::size_t>(lengthNumber) != type->count) {
    throwError(context, error, ErrorCode::ArgumentCountMismatch, type->count,
               lengthNumber < 0 ? 0 : static_cast<std::size_t>(lengthNumber));
    return false;
  }

  for (std::size_t i = 0; i < type->count; ++i) {
    JSValueRef elementValue =
        JSObjectGetPropertyAtIndex(context, array, static_cast<unsigned>(i), error);

    if (error && *error) return false;

    void *elementDestination = static_cast<unsigned char *>(destination) + i * elementSize;

    if (!convertArgument(context, elementValue, type->element, elementDestination, error,
                         stringStorage)) {
      return false;
    }
  }

  return true;
}

bool convertStructArgument(JSContextRef context, JSValueRef value,
                           const std::shared_ptr<Type> &type, void *destination, JSValueRef *error,
                           std::vector<std::vector<char>> &stringStorage) {
  if (!type || type->kind != TypeKind::Struct || !type->complete || type->fields.empty()) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  if (!JSValueIsObject(context, value)) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  if (isJSArray(context, value, error)) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  if (error && *error) return false;

  std::memset(destination, 0, type->size);

  JSObjectRef object = const_cast<JSObjectRef>(value);

  for (const Field &field : type->fields) {
    if (!field.type || field.offset > type->size || field.type->size > type->size - field.offset) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return false;
    }

    JSStringRef property = JSStringCreateWithUTF8CString(field.name.c_str());

    if (!property) {
      throwError(context, error, ErrorCode::NativeCallFailed);
      return false;
    }

    JSValueRef fieldValue = JSObjectGetProperty(context, object, property, error);

    JSStringRelease(property);

    if (error && *error) return false;

    if (!fieldValue || JSValueIsUndefined(context, fieldValue)) {
      throwError(context, error, ErrorCode::InvalidArgument);
      return false;
    }

    void *fieldDestination = static_cast<unsigned char *>(destination) + field.offset;

    if (!convertArgument(context, fieldValue, field.type, fieldDestination, error, stringStorage)) {
      return false;
    }
  }

  return true;
}

bool convertArgument(JSContextRef context, JSValueRef value, const std::shared_ptr<Type> &type,
                     void *destination, JSValueRef *error,
                     std::vector<std::vector<char>> &stringStorage) {
  if (!type || !destination) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return false;
  }

  switch (type->kind) {
  case TypeKind::Bool:
    if (!JSValueIsBoolean(context, value)) {
      throwError(context, error, ErrorCode::InvalidArgument);
      return false;
    }

    *static_cast<uint8_t *>(destination) = JSValueToBoolean(context, value) ? 1 : 0;

    return true;

  case TypeKind::Char:
    return convertNumberToInteger(context, value, *static_cast<char *>(destination), error);

  case TypeKind::Int8:
    return convertNumberToInteger(context, value, *static_cast<int8_t *>(destination), error);

  case TypeKind::UInt8:
    return convertNumberToInteger(context, value, *static_cast<uint8_t *>(destination), error);

  case TypeKind::Int16:
    return convertNumberToInteger(context, value, *static_cast<int16_t *>(destination), error);

  case TypeKind::UInt16:
    return convertNumberToInteger(context, value, *static_cast<uint16_t *>(destination), error);

  case TypeKind::Int32:
    return convertNumberToInteger(context, value, *static_cast<int32_t *>(destination), error);

  case TypeKind::UInt32:
    return convertNumberToInteger(context, value, *static_cast<uint32_t *>(destination), error);

  case TypeKind::Int64:
    return convertInt64(context, value, *static_cast<int64_t *>(destination), error);

  case TypeKind::UInt64:
    return convertUInt64(context, value, *static_cast<uint64_t *>(destination), error);

  case TypeKind::Size:
    if (sizeof(std::size_t) == sizeof(uint64_t)) {
      uint64_t converted = 0;

      if (!convertUInt64(context, value, converted, error)) return false;

      *static_cast<std::size_t *>(destination) = static_cast<std::size_t>(converted);

      return true;
    }

    return convertNumberToInteger(context, value, *static_cast<std::size_t *>(destination), error);

  case TypeKind::SSize:
    if (sizeof(std::ptrdiff_t) == sizeof(int64_t)) {
      int64_t converted = 0;

      if (!convertInt64(context, value, converted, error)) return false;

      *static_cast<std::ptrdiff_t *>(destination) = static_cast<std::ptrdiff_t>(converted);

      return true;
    }

    return convertNumberToInteger(context, value, *static_cast<std::ptrdiff_t *>(destination),
                                  error);

  case TypeKind::Float:
    return convertNumberToFloating(context, value, *static_cast<float *>(destination), error);

  case TypeKind::Double:
    return convertNumberToFloating(context, value, *static_cast<double *>(destination), error);

  case TypeKind::Pointer:
    if (isCharPointer(type)) {
      return convertCharPointerArgument(context, value, destination, stringStorage, error);
    }

    if (type->element && type->element->kind == TypeKind::Function) {
      return convertFunctionPointerArgument(context, value, destination, error);
    }

    return convertPointerArgument(context, value, destination, error);

  case TypeKind::Struct:
    return convertStructArgument(context, value, type, destination, error, stringStorage);

  case TypeKind::Array:
    return convertArrayArgument(context, value, type, destination, error, stringStorage);

  case TypeKind::Function:
  case TypeKind::Void: throwError(context, error, ErrorCode::UnsupportedArgumentType); return false;
  }

  throwError(context, error, ErrorCode::InvalidArgument);
  return false;
}

std::shared_ptr<Type> inferVariadicType(JSContextRef context, JSValueRef value, JSValueRef *error) {
  if (!value) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return nullptr;
  }

  if (JSValueIsNumber(context, value)) {
    double number = 0;

    if (!getNumber(context, value, number, error)) return nullptr;

    if (std::isfinite(number) && std::trunc(number) == number && isSafeInteger(number) &&
        number >= static_cast<double>(std::numeric_limits<int32_t>::min()) &&
        number <= static_cast<double>(std::numeric_limits<int32_t>::max())) {
      auto type = makeType(TypeKind::Int32);

      if (!type || !prepareType(type)) {
        throwError(context, error, ErrorCode::InvalidSignature);
        return nullptr;
      }

      return type;
    }

    auto type = makeType(TypeKind::Double);

    if (!type || !prepareType(type)) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return nullptr;
    }

    return type;
  }

  if (JSValueIsBigInt(context, value)) {
    auto type = makeType(TypeKind::Int64);

    if (!type || !prepareType(type)) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return nullptr;
    }

    return type;
  }

  if (JSValueIsString(context, value)) {
    auto charType = makeType(TypeKind::Char);

    if (!charType || !prepareType(charType)) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return nullptr;
    }

    auto pointerType = makeType(TypeKind::Pointer);

    if (!pointerType) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return nullptr;
    }

    pointerType->element = std::move(charType);
    pointerType->size = sizeof(void *);
    pointerType->alignment = alignof(void *);
    pointerType->complete = true;
    pointerType->ffi = &ffi_type_pointer;

    return pointerType;
  }

  throwError(context, error, ErrorCode::InvalidArgument);
  return nullptr;
}

JSValueRef convertCharPointerReturn(JSContextRef context, const void *value, JSValueRef *error) {
  const char *string = *static_cast<const char *const *>(value);

  if (!string) return JSValueMakeNull(context);

  JSStringRef jsString = JSStringCreateWithUTF8CString(string);

  if (!jsString) {
    throwError(context, error, ErrorCode::InvalidCString);
    return JSValueMakeUndefined(context);
  }

  JSValueRef result = JSValueMakeString(context, jsString);

  JSStringRelease(jsString);

  return result;
}

JSValueRef convertReturn(JSContextRef context, const std::shared_ptr<Type> &type, const void *value,
                         JSValueRef *error);

JSValueRef convertArrayReturn(JSContextRef context, const std::shared_ptr<Type> &type,
                              const void *value, JSValueRef *error) {
  if (!type || type->kind != TypeKind::Array || !type->element || type->count == 0) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return JSValueMakeUndefined(context);
  }

  const std::size_t elementSize = type->element->size;

  if (elementSize == 0) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return JSValueMakeUndefined(context);
  }

  std::vector<JSValueRef> elements;

  try {
    elements.reserve(type->count);
  } catch (...) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return JSValueMakeUndefined(context);
  }

  for (std::size_t i = 0; i < type->count; ++i) {
    const void *elementValue = static_cast<const unsigned char *>(value) + i * elementSize;

    JSValueRef element = convertReturn(context, type->element, elementValue, error);

    if (error && *error) return JSValueMakeUndefined(context);

    elements.push_back(element);
  }

  JSObjectRef result = JSObjectMakeArray(context, elements.size(), elements.data(), error);

  if (!result) return JSValueMakeUndefined(context);

  return result;
}

JSValueRef convertStructReturn(JSContextRef context, const std::shared_ptr<Type> &type,
                               const void *value, JSValueRef *error) {
  if (!type || type->kind != TypeKind::Struct || !type->complete || type->fields.empty()) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return JSValueMakeUndefined(context);
  }

  JSObjectRef object = JSObjectMake(context, nullptr, nullptr);

  if (!object) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return JSValueMakeUndefined(context);
  }

  for (const Field &field : type->fields) {
    if (!field.type || field.offset > type->size || field.type->size > type->size - field.offset) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return JSValueMakeUndefined(context);
    }

    const void *fieldValue = static_cast<const unsigned char *>(value) + field.offset;

    JSValueRef jsValue = convertReturn(context, field.type, fieldValue, error);

    if (error && *error) return JSValueMakeUndefined(context);

    JSStringRef property = JSStringCreateWithUTF8CString(field.name.c_str());

    if (!property) {
      throwError(context, error, ErrorCode::NativeCallFailed);
      return JSValueMakeUndefined(context);
    }

    JSObjectSetProperty(context, object, property, jsValue, kJSPropertyAttributeNone, error);

    JSStringRelease(property);

    if (error && *error) return JSValueMakeUndefined(context);
  }

  return object;
}

JSValueRef convertReturn(JSContextRef context, const std::shared_ptr<Type> &type, const void *value,
                         JSValueRef *error) {
  if (!type) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return JSValueMakeUndefined(context);
  }

  if (type->kind == TypeKind::Void) { return JSValueMakeUndefined(context); }

  if (!value) {
    throwError(context, error, ErrorCode::InvalidValue);
    return JSValueMakeUndefined(context);
  }

  switch (type->kind) {
  case TypeKind::Bool:
    return JSValueMakeBoolean(context, *static_cast<const uint8_t *>(value) != 0);

  case TypeKind::Char:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const char *>(value)));

  case TypeKind::Int8:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const int8_t *>(value)));

  case TypeKind::UInt8:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const uint8_t *>(value)));

  case TypeKind::Int16:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const int16_t *>(value)));

  case TypeKind::UInt16:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const uint16_t *>(value)));

  case TypeKind::Int32:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const int32_t *>(value)));

  case TypeKind::UInt32:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const uint32_t *>(value)));

  case TypeKind::Int64:
    return JSBigIntCreateWithInt64(context, *static_cast<const int64_t *>(value), error);

  case TypeKind::UInt64:
    return JSBigIntCreateWithUInt64(context, *static_cast<const uint64_t *>(value), error);

  case TypeKind::Size:
    if (sizeof(std::size_t) == sizeof(uint64_t)) {
      return JSBigIntCreateWithUInt64(
          context, static_cast<uint64_t>(*static_cast<const std::size_t *>(value)), error);
    }

    return JSValueMakeNumber(context,
                             static_cast<double>(*static_cast<const std::size_t *>(value)));

  case TypeKind::SSize:
    if (sizeof(std::ptrdiff_t) == sizeof(int64_t)) {
      return JSBigIntCreateWithInt64(
          context, static_cast<int64_t>(*static_cast<const std::ptrdiff_t *>(value)), error);
    }

    return JSValueMakeNumber(context,
                             static_cast<double>(*static_cast<const std::ptrdiff_t *>(value)));

  case TypeKind::Float:
    return JSValueMakeNumber(context, static_cast<double>(*static_cast<const float *>(value)));

  case TypeKind::Double: return JSValueMakeNumber(context, *static_cast<const double *>(value));

  case TypeKind::Pointer: {
    void *address = *static_cast<void *const *>(value);

    if (!address) return JSValueMakeNull(context);

    if (isCharPointer(type)) { return convertCharPointerReturn(context, value, error); }

    if (type->element && type->element->kind == TypeKind::Function) {
      const std::shared_ptr<Type> &functionType = type->element;

      if (!functionType->functionSignature) {
        throwError(context, error, ErrorCode::InvalidSignature);
        return JSValueMakeUndefined(context);
      }

      Signature signature;

      signature.returns = functionType->functionSignature->returns;
      signature.args = functionType->functionSignature->args;
      signature.variadic = functionType->functionSignature->variadic;

      if (!prepareSignature(signature)) {
        throwError(context, error, ErrorCode::SignaturePreparationFailed);
        return JSValueMakeUndefined(context);
      }

      JSObjectRef function = makeNativeFunction(context, address, std::move(signature), error);

      if (!function) return JSValueMakeUndefined(context);

      return function;
    }

    return makeNativePointer(context, reinterpret_cast<std::uintptr_t>(address));
  }

  case TypeKind::Struct: return convertStructReturn(context, type, value, error);

  case TypeKind::Array: return convertArrayReturn(context, type, value, error);

  case TypeKind::Function:
    throwError(context, error, ErrorCode::UnsupportedReturnType);
    return JSValueMakeUndefined(context);

  case TypeKind::Void: return JSValueMakeUndefined(context);
  }

  throwError(context, error, ErrorCode::InvalidValue);
  return JSValueMakeUndefined(context);
}

std::size_t storageWords(std::size_t size) {
  const std::size_t wordSize = sizeof(std::max_align_t);

  if (size == 0) return 1;

  if (size > std::numeric_limits<std::size_t>::max() - (wordSize - 1)) { return 0; }

  return (size + wordSize - 1) / wordSize;
}

bool validateSignature(JSContextRef context, const Signature &signature, JSValueRef *error) {
  if (!signature.prepared) {
    throwError(context, error, ErrorCode::SignaturePreparationFailed);
    return false;
  }

  if (!signature.returns || !signature.returns->ffi || !signature.returns->complete) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return false;
  }

  if (signature.args.size() != signature.ffiArgs.size() ||
      signature.args.size() > std::numeric_limits<unsigned int>::max() ||
      signature.cif.nargs != static_cast<unsigned int>(signature.args.size()) ||
      signature.cif.rtype != signature.returns->ffi) {
    throwError(context, error, ErrorCode::InvalidArgumentList);
    return false;
  }

  if (signature.args.empty()) {
    if (signature.cif.arg_types != nullptr) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return false;
    }

    return true;
  }

  if (signature.cif.arg_types != signature.ffiArgs.data()) {
    throwError(context, error, ErrorCode::InvalidSignature);
    return false;
  }

  for (std::size_t i = 0; i < signature.args.size(); ++i) {
    const std::shared_ptr<Type> &type = signature.args[i];

    if (!type || !type->ffi || !type->complete || signature.ffiArgs[i] != type->ffi) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return false;
    }
  }

  return true;
}

} // namespace

bool jsValueToNative(JSContextRef context, JSValueRef value, const std::shared_ptr<Type> &type,
                     void *destination, JSValueRef *error) {
  try {
    std::vector<std::vector<char>> stringStorage;

    return convertArgument(context, value, type, destination, error, stringStorage);
  } catch (...) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return false;
  }
}

JSValueRef nativeToJSValue(JSContextRef context, const std::shared_ptr<Type> &type,
                           const void *value, JSValueRef *error) {
  try {
    return convertReturn(context, type, value, error);
  } catch (...) {
    throwError(context, error, ErrorCode::NativeCallFailed);
    return JSValueMakeUndefined(context);
  }
}

JSValueRef callNativeFunction(JSContextRef context, const NativeFunctionState &function,
                              const Signature &signature, size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  try {
    if (!function.address) {
      throwError(context, error, ErrorCode::InvalidNativeFunction);
      return JSValueMakeUndefined(context);
    }

    if (!validateSignature(context, signature, error)) { return JSValueMakeUndefined(context); }

    if (signature.variadic) {
      if (argumentCount < signature.args.size()) {
        throwError(context, error, ErrorCode::ArgumentCountMismatch, signature.args.size(),
                   argumentCount);
        return JSValueMakeUndefined(context);
      }
    } else {
      if (argumentCount != signature.args.size()) {
        throwError(context, error, ErrorCode::ArgumentCountMismatch, signature.args.size(),
                   argumentCount);
        return JSValueMakeUndefined(context);
      }
    }

    if (argumentCount != 0 && !arguments) {
      throwError(context, error, ErrorCode::InvalidArgument);
      return JSValueMakeUndefined(context);
    }

    std::vector<std::shared_ptr<Type>> argumentTypes;
    argumentTypes.reserve(argumentCount);

    for (std::size_t i = 0; i < signature.args.size(); ++i) {
      argumentTypes.push_back(signature.args[i]);
    }

    if (signature.variadic) {
      for (std::size_t i = signature.args.size(); i < argumentCount; ++i) {
        std::shared_ptr<Type> type = inferVariadicType(context, arguments[i], error);

        if (!type) return JSValueMakeUndefined(context);

        argumentTypes.push_back(std::move(type));
      }
    }

    std::vector<std::vector<std::max_align_t>> storage;
    std::vector<void *> values;
    std::vector<std::vector<char>> stringStorage;

    storage.reserve(argumentCount);
    values.reserve(argumentCount);
    stringStorage.reserve(argumentCount);

    for (std::size_t i = 0; i < argumentTypes.size(); ++i) {
      const std::shared_ptr<Type> &type = argumentTypes[i];

      if (!type || !type->ffi || !type->complete) {
        throwError(context, error, ErrorCode::InvalidSignature);
        return JSValueMakeUndefined(context);
      }

      if (type->size == 0) {
        throwError(context, error, ErrorCode::InvalidArgument);
        return JSValueMakeUndefined(context);
      }

      const std::size_t words = storageWords(type->size);

      if (words == 0) {
        throwError(context, error, ErrorCode::IntegerOutOfRange);
        return JSValueMakeUndefined(context);
      }

      storage.emplace_back(words);

      values.push_back(storage.back().data());

      if (!convertArgument(context, arguments[i], type, values.back(), error, stringStorage)) {
        return JSValueMakeUndefined(context);
      }
    }

    const std::shared_ptr<Type> &returnType = signature.returns;

    if (!returnType || !returnType->ffi || !returnType->complete) {
      throwError(context, error, ErrorCode::InvalidSignature);
      return JSValueMakeUndefined(context);
    }

    ffi_cif temporaryCif{};

    ffi_cif *cif = const_cast<ffi_cif *>(&signature.cif);

    std::vector<ffi_type *> variadicFFIArgs;

    if (signature.variadic) {
      variadicFFIArgs.reserve(argumentTypes.size());

      for (const std::shared_ptr<Type> &type : argumentTypes) {
        variadicFFIArgs.push_back(type->ffi);
      }

      const unsigned int fixedCount = static_cast<unsigned int>(signature.args.size());

      const unsigned int totalCount = static_cast<unsigned int>(variadicFFIArgs.size());

      const ffi_status status =
          ffi_prep_cif_var(&temporaryCif, FFI_DEFAULT_ABI, fixedCount, totalCount, returnType->ffi,
                           variadicFFIArgs.data());

      if (status != FFI_OK) {
        throwError(context, error, ErrorCode::SignaturePreparationFailed);
        return JSValueMakeUndefined(context);
      }

      cif = &temporaryCif;
    }

    void **argumentValues = values.empty() ? nullptr : values.data();

    if (returnType->kind == TypeKind::Void) {
      const int signalNumber = invokeProtectedNativeCall(
          cif, reinterpret_cast<void (*)(void)>(function.address), nullptr, argumentValues);

      if (signalNumber != 0) {
        throwError(context, error,
                   signalNumber == SIGSEGV ? ErrorCode::NativeSegmentationFault
                                           : ErrorCode::NativeCrash);

        return JSValueMakeUndefined(context);
      }

      return JSValueMakeUndefined(context);
    }

    std::size_t returnSize = returnType->size;

    if (returnSize < sizeof(ffi_arg)) { returnSize = sizeof(ffi_arg); }

    const std::size_t returnWords = storageWords(returnSize);

    if (returnWords == 0) {
      throwError(context, error, ErrorCode::IntegerOutOfRange);
      return JSValueMakeUndefined(context);
    }

    std::vector<std::max_align_t> returnStorage(returnWords);

    const int signalNumber =
        invokeProtectedNativeCall(cif, reinterpret_cast<void (*)(void)>(function.address),
                                  returnStorage.data(), argumentValues);

    if (signalNumber != 0) {
      throwError(context, error,
                 signalNumber == SIGSEGV ? ErrorCode::NativeSegmentationFault
                                         : ErrorCode::NativeCrash);

      return JSValueMakeUndefined(context);
    }

    return convertReturn(context, returnType, returnStorage.data(), error);

  } catch (...) {
    throwError(context, error, ErrorCode::NativeCallFailed);

    return JSValueMakeUndefined(context);
  }
}

}} // namespace edon::ffi
