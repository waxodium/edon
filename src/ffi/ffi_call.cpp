#include "ffi_call.hpp"
#include "ffi_memory.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace edon {
namespace ffi {

namespace {

void throwError(JSContextRef context, JSValueRef *error,
                const std::string &message) {
  if (!error)
    return;

  JSStringRef string = JSStringCreateWithUTF8CString(message.c_str());

  if (!string) {
    *error = nullptr;
    return;
  }

  JSValueRef messageValue = JSValueMakeString(context, string);

  *error = JSObjectMakeError(context, 1, &messageValue, nullptr);

  JSStringRelease(string);
}

const char *jsTypeName(JSContextRef context, JSValueRef value) {
  if (JSValueIsUndefined(context, value))
    return "undefined";

  if (JSValueIsNull(context, value))
    return "null";

  if (JSValueIsBoolean(context, value))
    return "boolean";

  if (JSValueIsBigInt(context, value))
    return "bigint";

  if (JSValueIsNumber(context, value))
    return "number";

  if (JSValueIsString(context, value))
    return "string";

  if (JSValueIsObject(context, value))
    return "object";

  return "unknown";
}

std::string jsValueDescription(JSContextRef context, JSValueRef value) {
  if (JSValueIsUndefined(context, value))
    return "undefined";

  if (JSValueIsNull(context, value))
    return "null";

  if (JSValueIsBoolean(context, value))
    return JSValueToBoolean(context, value) ? "true" : "false";

  JSStringRef string = JSValueToStringCopy(context, value, nullptr);

  if (!string)
    return jsTypeName(context, value);

  const std::size_t size = JSStringGetMaximumUTF8CStringSize(string);

  if (size == 0) {
    JSStringRelease(string);
    return jsTypeName(context, value);
  }

  std::vector<char> buffer(size);

  const std::size_t length =
      JSStringGetUTF8CString(string, buffer.data(), buffer.size());

  JSStringRelease(string);

  if (length == 0)
    return jsTypeName(context, value);

  return std::string(buffer.data(), length - 1);
}

std::string argumentPrefix(std::size_t index, const char *type) {
  return "FFI: argument " + std::to_string(index) + " (" + type + ")";
}

bool getNumber(JSContextRef context, JSValueRef value, double &number,
               JSValueRef *error) {
  JSValueRef localError = nullptr;
  JSValueRef *targetError = error ? error : &localError;

  number = JSValueToNumber(context, value, targetError);

  return !*targetError;
}

bool isSafeInteger(double value) {
  return std::isfinite(value) && std::trunc(value) == value &&
         value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

template <typename T>
bool convertNumberToInteger(JSContextRef context, JSValueRef value, T &output,
                            JSValueRef *error, const char *type,
                            std::size_t index) {
  const std::string prefix = argumentPrefix(index, type);

  if (!JSValueIsNumber(context, value)) {
    throwError(context, error,
               prefix + " requires a Number, got " +
                   jsTypeName(context, value) + " (" +
                   jsValueDescription(context, value) + ")");
    return false;
  }

  double number = 0;

  if (!getNumber(context, value, number, error))
    return false;

  if (!isSafeInteger(number)) {
    throwError(context, error,
               prefix + " requires a safe integer Number; got " +
                   jsValueDescription(context, value));
    return false;
  }

  const long double numericValue = static_cast<long double>(number);

  const long double minimum =
      static_cast<long double>(std::numeric_limits<T>::lowest());

  const long double maximum =
      static_cast<long double>(std::numeric_limits<T>::max());

  if (numericValue < minimum || numericValue > maximum) {
    throwError(context, error,
               prefix + " value " + jsValueDescription(context, value) +
                   " is outside the " + type + " range");
    return false;
  }

  output = static_cast<T>(number);
  return true;
}

bool valueToString(JSContextRef context, JSValueRef value, std::string &output,
                   JSValueRef *error) {
  JSStringRef string = JSValueToStringCopy(context, value, error);

  if (error && *error)
    return false;

  if (!string)
    return false;

  const std::size_t size = JSStringGetMaximumUTF8CStringSize(string);

  if (size == 0) {
    JSStringRelease(string);
    return false;
  }

  std::vector<char> buffer(size);

  const std::size_t length =
      JSStringGetUTF8CString(string, buffer.data(), buffer.size());

  JSStringRelease(string);

  if (length == 0)
    return false;

  output.assign(buffer.data(), length - 1);

  return true;
}

bool parseUnsigned(const std::string &text, uint64_t maximum,
                   uint64_t &output) {
  if (text.empty())
    return false;

  std::size_t index = 0;

  if (text[0] == '+')
    index = 1;

  if (index == text.size())
    return false;

  uint64_t result = 0;

  for (; index < text.size(); ++index) {
    const char character = text[index];

    if (character < '0' || character > '9')
      return false;

    const uint64_t digit = static_cast<uint64_t>(character - '0');

    if (result > (maximum - digit) / 10)
      return false;

    result = result * 10 + digit;
  }

  output = result;
  return true;
}

bool parseSigned(const std::string &text, int64_t minimum, int64_t maximum,
                 int64_t &output) {
  if (text.empty())
    return false;

  bool negative = false;
  std::size_t index = 0;

  if (text[0] == '-') {
    negative = true;
    index = 1;
  } else if (text[0] == '+') {
    index = 1;
  }

  if (index == text.size())
    return false;

  const uint64_t negativeLimit = static_cast<uint64_t>(-(minimum + 1)) + 1;

  const uint64_t limit =
      negative ? negativeLimit : static_cast<uint64_t>(maximum);

  uint64_t magnitude = 0;

  if (!parseUnsigned(text.substr(index), limit, magnitude))
    return false;

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

bool convertBigIntToInt64(JSContextRef context, JSValueRef value,
                          int64_t &output, JSValueRef *error, const char *type,
                          std::size_t index) {
  const std::string prefix = argumentPrefix(index, type);

  if (!JSValueIsBigInt(context, value)) {
    throwError(context, error, prefix + " requires a BigInt");
    return false;
  }

  std::string text;

  if (!valueToString(context, value, text, error))
    return false;

  if (!parseSigned(text, std::numeric_limits<int64_t>::min(),
                   std::numeric_limits<int64_t>::max(), output)) {
    throwError(context, error,
               prefix + " BigInt " + text + " is outside the int64 range [" +
                   std::to_string(std::numeric_limits<int64_t>::min()) + ", " +
                   std::to_string(std::numeric_limits<int64_t>::max()) + "]");
    return false;
  }

  return true;
}

bool convertBigIntToUInt64(JSContextRef context, JSValueRef value,
                           uint64_t &output, JSValueRef *error,
                           const char *type, std::size_t index) {
  const std::string prefix = argumentPrefix(index, type);

  if (!JSValueIsBigInt(context, value)) {
    throwError(context, error, prefix + " requires a BigInt");
    return false;
  }

  std::string text;

  if (!valueToString(context, value, text, error))
    return false;

  if (!parseUnsigned(text, std::numeric_limits<uint64_t>::max(), output)) {
    throwError(context, error,
               prefix + " BigInt " + text +
                   " is outside the uint64 range [0, " +
                   std::to_string(std::numeric_limits<uint64_t>::max()) + "]");
    return false;
  }

  return true;
}

bool convertInt64(JSContextRef context, JSValueRef value, int64_t &output,
                  JSValueRef *error, const char *type, std::size_t index) {
  if (JSValueIsBigInt(context, value)) {
    return convertBigIntToInt64(context, value, output, error, type, index);
  }

  return convertNumberToInteger(context, value, output, error, type, index);
}

bool convertUInt64(JSContextRef context, JSValueRef value, uint64_t &output,
                   JSValueRef *error, const char *type, std::size_t index) {
  if (JSValueIsBigInt(context, value)) {
    return convertBigIntToUInt64(context, value, output, error, type, index);
  }

  return convertNumberToInteger(context, value, output, error, type, index);
}

template <typename T>
bool convertNumberToFloating(JSContextRef context, JSValueRef value, T &output,
                             JSValueRef *error, const char *type,
                             std::size_t index) {
  const std::string prefix = argumentPrefix(index, type);

  if (!JSValueIsNumber(context, value)) {
    throwError(context, error,
               prefix + " requires a Number, got " +
                   jsTypeName(context, value) + " (" +
                   jsValueDescription(context, value) + ")");
    return false;
  }

  double number = 0;

  if (!getNumber(context, value, number, error))
    return false;

  if (!std::isfinite(number)) {
    throwError(context, error,
               prefix + " requires a finite Number; got " +
                   jsValueDescription(context, value));
    return false;
  }

  output = static_cast<T>(number);

  if (!std::isfinite(static_cast<double>(output))) {
    throwError(context, error,
               prefix + " value " + jsValueDescription(context, value) +
                   " is outside the representable range of " + type);
    return false;
  }

  return true;
}

bool convertPointerArgument(JSContextRef context, JSValueRef value,
                            void *destination, JSValueRef *error,
                            std::size_t index) {
  const std::string prefix = argumentPrefix(index, "pointer");

  if (JSValueIsNull(context, value)) {
    *static_cast<void **>(destination) = nullptr;
    return true;
  }

  if (JSValueIsObject(context, value)) {
    PointerState *pointer = getNativePointer(context, value);

    if (pointer) {
      std::uintptr_t address = 0;

      if (!getPointerValue(context, value, address, error))
        return false;

      if (address == 0) {
        throwError(context, error, prefix + " contains a null NativePointer");
        return false;
      }

      *static_cast<void **>(destination) = reinterpret_cast<void *>(address);

      return true;
    }

    void *data = nullptr;
    std::size_t size = 0;

    if (getBufferPointer(context, value, data, size, nullptr)) {
      (void)size;

      if (!data) {
        throwError(context, error,
                   prefix + " references an empty native ArrayBuffer");
        return false;
      }

      *static_cast<void **>(destination) = data;
      return true;
    }
  }

  throwError(context, error,
             prefix + " requires a NativePointer, native ArrayBuffer, or null");

  return false;
}

bool convertCStringArgument(JSContextRef context, JSValueRef value,
                            void *destination,
                            std::vector<std::vector<char>> &stringStorage,
                            JSValueRef *error, std::size_t index) {
  const std::string prefix = argumentPrefix(index, "cstring");

  if (JSValueIsNull(context, value)) {
    *static_cast<const char **>(destination) = nullptr;
    return true;
  }

  if (!JSValueIsString(context, value)) {
    throwError(context, error,
               prefix + " requires a String or null, got " +
                   jsTypeName(context, value) + " (" +
                   jsValueDescription(context, value) + ")");
    return false;
  }

  JSStringRef string = JSValueToStringCopy(context, value, error);

  if (error && *error)
    return false;

  if (!string) {
    throwError(context, error, prefix + " could not convert string");
    return false;
  }

  const std::size_t size = JSStringGetMaximumUTF8CStringSize(string);

  if (size == 0) {
    JSStringRelease(string);

    throwError(context, error, prefix + " string conversion failed");
    return false;
  }

  std::vector<char> storage(size);

  const std::size_t length =
      JSStringGetUTF8CString(string, storage.data(), storage.size());

  JSStringRelease(string);

  if (length == 0) {
    throwError(context, error, prefix + " string conversion failed");
    return false;
  }

  stringStorage.push_back(std::move(storage));

  *static_cast<const char **>(destination) = stringStorage.back().data();

  return true;
}

bool convertArgument(JSContextRef context, JSValueRef value,
                     const std::shared_ptr<Type> &type, void *destination,
                     JSValueRef *error, std::size_t index,
                     std::vector<std::vector<char>> &stringStorage) {
  if (!type) {
    throwError(context, error,
               "FFI: argument " + std::to_string(index) +
                   " has no native type");
    return false;
  }

  if (!destination) {
    throwError(context, error,
               "FFI: argument " + std::to_string(index) +
                   " has no native storage");
    return false;
  }

  switch (type->kind) {
  case TypeKind::Bool:
    if (!JSValueIsBoolean(context, value)) {
      throwError(context, error,
                 argumentPrefix(index, "bool") + " requires a Boolean, got " +
                     jsTypeName(context, value) + " (" +
                     jsValueDescription(context, value) + ")");
      return false;
    }

    *static_cast<uint8_t *>(destination) =
        JSValueToBoolean(context, value) ? 1 : 0;

    return true;

  case TypeKind::Int8:
    return convertNumberToInteger(context, value,
                                  *static_cast<int8_t *>(destination), error,
                                  "int8", index);

  case TypeKind::UInt8:
    return convertNumberToInteger(context, value,
                                  *static_cast<uint8_t *>(destination), error,
                                  "uint8", index);

  case TypeKind::Int16:
    return convertNumberToInteger(context, value,
                                  *static_cast<int16_t *>(destination), error,
                                  "int16", index);

  case TypeKind::UInt16:
    return convertNumberToInteger(context, value,
                                  *static_cast<uint16_t *>(destination), error,
                                  "uint16", index);

  case TypeKind::Int32:
    return convertNumberToInteger(context, value,
                                  *static_cast<int32_t *>(destination), error,
                                  "int32", index);

  case TypeKind::UInt32:
    return convertNumberToInteger(context, value,
                                  *static_cast<uint32_t *>(destination), error,
                                  "uint32", index);

  case TypeKind::Int64:
    return convertInt64(context, value, *static_cast<int64_t *>(destination),
                        error, "int64", index);

  case TypeKind::UInt64:
    return convertUInt64(context, value, *static_cast<uint64_t *>(destination),
                         error, "uint64", index);

  case TypeKind::Size:
    if (sizeof(std::size_t) == sizeof(uint64_t)) {
      uint64_t converted = 0;

      if (!convertUInt64(context, value, converted, error, "size", index))
        return false;

      *static_cast<std::size_t *>(destination) =
          static_cast<std::size_t>(converted);

      return true;
    }

    return convertNumberToInteger(context, value,
                                  *static_cast<std::size_t *>(destination),
                                  error, "size", index);

  case TypeKind::SSize:
    if (sizeof(std::ptrdiff_t) == sizeof(int64_t)) {
      int64_t converted = 0;

      if (!convertInt64(context, value, converted, error, "ssize", index))
        return false;

      *static_cast<std::ptrdiff_t *>(destination) =
          static_cast<std::ptrdiff_t>(converted);

      return true;
    }

    return convertNumberToInteger(context, value,
                                  *static_cast<std::ptrdiff_t *>(destination),
                                  error, "ssize", index);

  case TypeKind::Float:
    return convertNumberToFloating(context, value,
                                   *static_cast<float *>(destination), error,
                                   "float", index);

  case TypeKind::Double:
    return convertNumberToFloating(context, value,
                                   *static_cast<double *>(destination), error,
                                   "double", index);

  case TypeKind::Pointer:
    return convertPointerArgument(context, value, destination, error, index);

  case TypeKind::CString:
    return convertCStringArgument(context, value, destination, stringStorage,
                                  error, index);

  case TypeKind::Void:
    throwError(context, error,
               argumentPrefix(index, "void") + " is not a valid argument type");
    return false;

  case TypeKind::Struct:
  case TypeKind::Array:
    throwError(context, error,
               argumentPrefix(index, typeName(type->kind)) +
                   " is not supported by the native call layer");
    return false;
  }

  throwError(context, error,
             "FFI: argument " + std::to_string(index) +
                 " has an unknown native type");

  return false;
}

JSValueRef convertReturn(JSContextRef context,
                         const std::shared_ptr<Type> &type, const void *value,
                         JSValueRef *error) {
  if (!type) {
    throwError(context, error, "FFI: native function has no return type");
    return JSValueMakeUndefined(context);
  }

  if (type->kind == TypeKind::Void)
    return JSValueMakeUndefined(context);

  if (!value) {
    throwError(context, error,
               "FFI: native function returned without valid storage");
    return JSValueMakeUndefined(context);
  }

  switch (type->kind) {
  case TypeKind::Bool:
    return JSValueMakeBoolean(context,
                              *static_cast<const uint8_t *>(value) != 0);

  case TypeKind::Int8:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const int8_t *>(value)));

  case TypeKind::UInt8:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const uint8_t *>(value)));

  case TypeKind::Int16:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const int16_t *>(value)));

  case TypeKind::UInt16:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const uint16_t *>(value)));

  case TypeKind::Int32:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const int32_t *>(value)));

  case TypeKind::UInt32:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const uint32_t *>(value)));

  case TypeKind::Int64:
    return JSBigIntCreateWithInt64(context,
                                   *static_cast<const int64_t *>(value), error);

  case TypeKind::UInt64:
    return JSBigIntCreateWithUInt64(
        context, *static_cast<const uint64_t *>(value), error);

  case TypeKind::Size:
    if (sizeof(std::size_t) == sizeof(uint64_t)) {
      return JSBigIntCreateWithUInt64(
          context,
          static_cast<uint64_t>(*static_cast<const std::size_t *>(value)),
          error);
    }

    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const std::size_t *>(value)));

  case TypeKind::SSize:
    if (sizeof(std::ptrdiff_t) == sizeof(int64_t)) {
      return JSBigIntCreateWithInt64(
          context,
          static_cast<int64_t>(*static_cast<const std::ptrdiff_t *>(value)),
          error);
    }

    return JSValueMakeNumber(
        context,
        static_cast<double>(*static_cast<const std::ptrdiff_t *>(value)));

  case TypeKind::Float:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const float *>(value)));

  case TypeKind::Double:
    return JSValueMakeNumber(context, *static_cast<const double *>(value));

  case TypeKind::Pointer:
  case TypeKind::CString:
  case TypeKind::Struct:
  case TypeKind::Array:
    throwError(context, error,
               "FFI: native return type " + std::string(typeName(type->kind)) +
                   " is not supported by the native call layer");
    return JSValueMakeUndefined(context);

  case TypeKind::Void:
    return JSValueMakeUndefined(context);
  }

  throwError(context, error, "FFI: native function has an unknown native type");

  return JSValueMakeUndefined(context);
}

std::size_t storageWords(std::size_t size) {
  const std::size_t wordSize = sizeof(std::max_align_t);

  if (size == 0)
    return 1;

  if (size > std::numeric_limits<std::size_t>::max() - (wordSize - 1))
    return 0;

  return (size + wordSize - 1) / wordSize;
}

bool validateSignature(JSContextRef context, const Signature &signature,
                       JSValueRef *error) {
  if (!signature.prepared) {
    throwError(context, error, "FFI: libffi signature has not been prepared");
    return false;
  }

  if (!signature.returns || !signature.returns->ffi ||
      !signature.returns->complete) {
    throwError(context, error,
               "FFI: libffi signature has no valid return type");
    return false;
  }

  if (signature.args.size() != signature.ffiArgs.size()) {
    throwError(context, error,
               "FFI: prepared signature argument count is invalid");
    return false;
  }

  if (signature.args.size() > std::numeric_limits<unsigned int>::max()) {
    throwError(context, error, "FFI: signature contains too many arguments");
    return false;
  }

  if (signature.cif.nargs != static_cast<unsigned int>(signature.args.size())) {
    throwError(context, error,
               "FFI: prepared libffi argument count is invalid");
    return false;
  }

  if (signature.cif.rtype != signature.returns->ffi) {
    throwError(context, error, "FFI: prepared libffi return type is invalid");
    return false;
  }

  if (signature.args.empty()) {
    if (signature.cif.arg_types != nullptr) {
      throwError(context, error,
                 "FFI: prepared libffi argument storage is invalid");
      return false;
    }

    return true;
  }

  if (signature.cif.arg_types != signature.ffiArgs.data()) {
    throwError(context, error,
               "FFI: prepared libffi argument storage is invalid");
    return false;
  }

  for (std::size_t i = 0; i < signature.args.size(); ++i) {
    const std::shared_ptr<Type> &type = signature.args[i];

    if (!type || !type->ffi || !type->complete ||
        signature.ffiArgs[i] != type->ffi) {
      throwError(context, error,
                 "FFI: prepared libffi argument type is invalid");
      return false;
    }
  }

  return true;
}

} // namespace

bool jsValueToNative(JSContextRef context, JSValueRef value,
                     const std::shared_ptr<Type> &type, void *destination,
                     JSValueRef *error) {
  try {
    std::vector<std::vector<char>> stringStorage;

    return convertArgument(context, value, type, destination, error, 0,
                           stringStorage);
  } catch (const std::bad_alloc &) {
    throwError(context, error,
               "FFI: native argument conversion ran out of memory");
    return false;
  } catch (const std::exception &exception) {
    throwError(context, error,
               std::string("FFI: native argument conversion failed: ") +
                   exception.what());
    return false;
  } catch (...) {
    throwError(context, error, "FFI: native argument conversion failed");
    return false;
  }
}

JSValueRef nativeToJSValue(JSContextRef context,
                           const std::shared_ptr<Type> &type, const void *value,
                           JSValueRef *error) {
  try {
    return convertReturn(context, type, value, error);
  } catch (const std::bad_alloc &) {
    throwError(context, error,
               "FFI: native return conversion ran out of memory");
    return JSValueMakeUndefined(context);
  } catch (const std::exception &exception) {
    throwError(context, error,
               std::string("FFI: native return conversion failed: ") +
                   exception.what());
    return JSValueMakeUndefined(context);
  } catch (...) {
    throwError(context, error, "FFI: native return conversion failed");
    return JSValueMakeUndefined(context);
  }
}

JSValueRef callNativeFunction(JSContextRef context,
                              const NativeFunctionState &function,
                              const Signature &signature, size_t argumentCount,
                              const JSValueRef arguments[], JSValueRef *error) {
  try {
    if (!function.address) {
      throwError(context, error,
                 "FFI: cannot call native function '" + function.name +
                     "': symbol address is null");
      return JSValueMakeUndefined(context);
    }

    if (!validateSignature(context, signature, error))
      return JSValueMakeUndefined(context);

    if (argumentCount != signature.args.size()) {
      throwError(context, error,
                 "FFI: cannot call native function '" + function.name +
                     "': expected " + std::to_string(signature.args.size()) +
                     " argument(s), received " + std::to_string(argumentCount));
      return JSValueMakeUndefined(context);
    }

    if (argumentCount != 0 && !arguments) {
      throwError(context, error,
                 "FFI: cannot call native function '" + function.name +
                     "': argument storage is null");
      return JSValueMakeUndefined(context);
    }

    std::vector<std::vector<std::max_align_t>> storage;
    std::vector<void *> values;
    std::vector<std::vector<char>> stringStorage;

    storage.reserve(signature.args.size());
    values.reserve(signature.args.size());
    stringStorage.reserve(signature.args.size());

    for (std::size_t i = 0; i < signature.args.size(); ++i) {
      const std::shared_ptr<Type> &type = signature.args[i];

      if (!type || !type->ffi || !type->complete) {
        throwError(context, error,
                   "FFI: cannot call native function '" + function.name +
                       "': argument " + std::to_string(i) +
                       " has an unprepared native type");
        return JSValueMakeUndefined(context);
      }

      if (type->size == 0) {
        throwError(context, error,
                   "FFI: cannot call native function '" + function.name +
                       "': argument " + std::to_string(i) +
                       " has zero-sized native storage");
        return JSValueMakeUndefined(context);
      }

      const std::size_t words = storageWords(type->size);

      if (words == 0) {
        throwError(context, error,
                   "FFI: cannot call native function '" + function.name +
                       "': argument " + std::to_string(i) +
                       " storage size overflow");
        return JSValueMakeUndefined(context);
      }

      storage.emplace_back(words);

      values.push_back(storage.back().data());

      if (!convertArgument(context, arguments[i], type, values.back(), error, i,
                           stringStorage))
        return JSValueMakeUndefined(context);
    }

    const std::shared_ptr<Type> &returnType = signature.returns;

    if (!returnType || !returnType->ffi || !returnType->complete) {
      throwError(context, error,
                 "FFI: cannot call native function '" + function.name +
                     "': return type is not prepared");
      return JSValueMakeUndefined(context);
    }

    ffi_cif *cif = const_cast<ffi_cif *>(&signature.cif);

    void **argumentValues = values.empty() ? nullptr : values.data();

    if (returnType->kind == TypeKind::Void) {
      ffi_call(cif, FFI_FN(function.address), nullptr, argumentValues);

      return JSValueMakeUndefined(context);
    }

    std::size_t returnSize = returnType->size;

    if (returnSize < sizeof(ffi_arg))
      returnSize = sizeof(ffi_arg);

    const std::size_t returnWords = storageWords(returnSize);

    if (returnWords == 0) {
      throwError(context, error, "FFI: native return storage size overflow");
      return JSValueMakeUndefined(context);
    }

    std::vector<std::max_align_t> returnStorage(returnWords);

    ffi_call(cif, FFI_FN(function.address), returnStorage.data(),
             argumentValues);

    return convertReturn(context, returnType, returnStorage.data(), error);
  } catch (const std::bad_alloc &) {
    throwError(context, error, "FFI: native call ran out of memory");
    return JSValueMakeUndefined(context);
  } catch (const std::exception &exception) {
    throwError(context, error,
               std::string("FFI: native call failed: ") + exception.what());
    return JSValueMakeUndefined(context);
  } catch (...) {
    throwError(context, error, "FFI: native call failed");
    return JSValueMakeUndefined(context);
  }
}

} // namespace ffi
} // namespace edon
