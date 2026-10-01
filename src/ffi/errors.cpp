#include "errors.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace edon { namespace ffi {

namespace {

struct ErrorDefinition {
  const char *type;
  const char *message;
};

const std::unordered_map<ErrorCode, ErrorDefinition> &definitions() {
  static const std::unordered_map<ErrorCode, ErrorDefinition> values = {
      {ErrorCode::InvalidArgumentCount, {"TypeError", "Invalid argument count"}},
      {ErrorCode::ArgumentCountMismatch,
       {"TypeError", "Invalid argument count: expected %zu, got %zu"}},
      {ErrorCode::InvalidArgument, {"TypeError", "Invalid argument"}},
      {ErrorCode::InvalidArgumentList, {"TypeError", "Invalid argument list"}},
      {ErrorCode::InvalidSignature, {"TypeError", "Invalid native function signature"}},
      {ErrorCode::SignaturePreparationFailed,
       {"FFIError", "Failed to prepare native function signature"}},
      {ErrorCode::MissingReturnType,
       {"TypeError", "Native function signature is missing a return type"}},
      {ErrorCode::MissingArgumentTypes,
       {"TypeError", "Native function signature is missing argument types"}},
      {ErrorCode::MissingArgumentType,
       {"TypeError", "Native function signature is missing argument type at index %zu"}},
      {ErrorCode::EmptyReturnType, {"TypeError", "Native function return type cannot be empty"}},
      {ErrorCode::EmptyArgumentType,
       {"TypeError", "Native function argument type at index %zu cannot be empty"}},
      {ErrorCode::InvalidType, {"TypeError", "Invalid native type: %s"}},
      {ErrorCode::UnknownReturnType, {"TypeError", "Unknown native return type: %s"}},
      {ErrorCode::UnknownArgumentType, {"TypeError", "Unknown native argument type: %s"}},
      {ErrorCode::UnsupportedType, {"TypeError", "Unsupported native type: %s"}},
      {ErrorCode::UnsupportedReturnType, {"TypeError", "Unsupported native return type: %s"}},
      {ErrorCode::UnsupportedArgumentType, {"TypeError", "Unsupported native argument type: %s"}},
      {ErrorCode::InvalidNativeModule, {"FFIError", "Invalid native module"}},
      {ErrorCode::InvalidNativeFunction, {"FFIError", "Invalid native function"}},
      {ErrorCode::EmptyFunctionName, {"TypeError", "Native function name cannot be empty"}},
      {ErrorCode::FunctionNotFound, {"FFIError", "Native function not found: %s"}},
      {ErrorCode::EmptyModulePath, {"TypeError", "Native library path cannot be empty"}},
      {ErrorCode::ModuleLoadFailed, {"FFIError", "Failed to load native library: %s"}},
      {ErrorCode::ModuleNotFound, {"FFIError", "Native library not found: %s"}},
      {ErrorCode::ModuleSystemUnavailable, {"FFIError", "Native module system is unavailable"}},
      {ErrorCode::MissingSource, {"TypeError", "C source code is required"}},
      {ErrorCode::InvalidSource, {"TypeError", "Invalid C source code"}},
      {ErrorCode::EmptySource, {"TypeError", "C source code cannot be empty"}},
      {ErrorCode::EmptyTemplate, {"TypeError", "C template cannot be empty"}},
      {ErrorCode::TemplateSubstitution,
       {"CCompileError", "Failed to process C template substitution"}},
      {ErrorCode::CompilationFailed, {"CCompileError", "C compilation failed"}},
      {ErrorCode::InvalidPointer, {"TypeError", "Invalid native pointer"}},
      {ErrorCode::InvalidMemoryArgument, {"TypeError", "Invalid native memory argument"}},
      {ErrorCode::InvalidBuffer, {"TypeError", "Invalid ArrayBuffer"}},
      {ErrorCode::InvalidBufferSize, {"RangeError", "Invalid buffer size"}},
      {ErrorCode::InvalidAllocation, {"FFIError", "Invalid native allocation"}},
      {ErrorCode::AllocationFailed, {"RangeError", "Failed to allocate native memory"}},
      {ErrorCode::FreedMemory, {"UseAfterFreeError", "Attempted to access freed native memory"}},
      {ErrorCode::UntrackedMemory,
       {"InvalidPointerError", "Attempted to access untracked native memory"}},
      {ErrorCode::AlreadyFreed, {"DoubleFreeError", "Native memory has already been freed"}},
      {ErrorCode::NotOwnedMemory,
       {"InvalidPointerError", "Native memory is not owned by this runtime"}},
      {ErrorCode::NullPointer, {"NullPointerError", "Native pointer is null"}},
      {ErrorCode::AddressOfInvalidArgument,
       {"TypeError", "addressOf expects a managed ArrayBuffer or native pointer"}},
      {ErrorCode::FreeInvalidArgument,
       {"TypeError", "free expects a NativePointer or managed ArrayBuffer"}},
      {ErrorCode::ExpectedNumber, {"TypeError", "Expected a number"}},
      {ErrorCode::ExpectedString, {"TypeError", "Expected a string"}},
      {ErrorCode::ExpectedObject, {"TypeError", "Expected an object"}},
      {ErrorCode::ExpectedFunction, {"TypeError", "Expected a function"}},
      {ErrorCode::ExpectedPointer, {"TypeError", "Expected a native pointer"}},
      {ErrorCode::InvalidValue, {"TypeError", "Invalid value"}},
      {ErrorCode::InvalidInteger, {"TypeError", "Expected a valid integer"}},
      {ErrorCode::IntegerOutOfRange, {"RangeError", "Integer value is outside the native range"}},
      {ErrorCode::UnsafeInteger,
       {"RangeError", "Integer must be a safe JavaScript integer or BigInt"}},
      {ErrorCode::InvalidFloat, {"TypeError", "Expected a finite floating-point number"}},
      {ErrorCode::FloatOverflow,
       {"RangeError", "Floating-point value is outside the native range"}},
      {ErrorCode::InvalidCString, {"TypeError", "Expected a string or null for cstring"}},
      {ErrorCode::CallbackInvalidArgument, {"TypeError", "Invalid callback argument"}},
      {ErrorCode::CallbackAlreadyDestroyed,
       {"CallbackError", "Native callback has already been destroyed"}},
      {ErrorCode::CallbackCreationFailed, {"CallbackError", "Failed to create native callback"}},
      {ErrorCode::NativeCallFailed, {"NativeCallError", "Native function call failed"}},
      {ErrorCode::NativeCrash, {"NativeCrashError", "Native code crashed during execution"}},
      {ErrorCode::NativeSegmentationFault, {"CNativeError", "Segmentation fault (core dumped)"}},
      {ErrorCode::ObjectCreationFailed, {"FFIError", "Failed to create native object"}},
      {ErrorCode::PropertyCreationFailed, {"FFIError", "Failed to create native property"}},
      {ErrorCode::FunctionCreationFailed, {"FFIError", "Failed to create native function"}},
      {ErrorCode::StateCreationFailed, {"FFIError", "Failed to create native compiler state"}},
      {ErrorCode::InternalError, {"FFIError", "Internal FFI error"}}};

  return values;
}

const ErrorDefinition *findDefinition(ErrorCode code) {
  const auto &map = definitions();
  const auto it = map.find(code);

  if (it == map.end()) return nullptr;

  return &it->second;
}

std::string replaceFirst(const std::string &source, const std::string &token,
                         const std::string &replacement) {
  const std::size_t position = source.find(token);

  if (position == std::string::npos) return source;

  std::string result = source;
  result.replace(position, token.size(), replacement);

  return result;
}

std::string formatString(const std::string &format, const std::string &value) {
  return replaceFirst(format, "%s", value);
}

std::string formatSize(const std::string &format, std::size_t value) {
  return replaceFirst(format, "%zu", std::to_string(value));
}

std::string formatSizes(const std::string &format, std::size_t first, std::size_t second) {
  std::string result = formatSize(format, first);
  return formatSize(result, second);
}

std::string formatStringAndSize(const std::string &format, const std::string &value,
                                std::size_t index) {
  std::string result = formatString(format, value);
  return formatSize(result, index);
}

std::string formatStringAndInt(const std::string &format, const std::string &value, int number) {
  std::string result = formatString(format, value);
  return replaceFirst(result, "%d", std::to_string(number));
}

JSStringRef makeString(const std::string &value) {
  return JSStringCreateWithUTF8CString(value.c_str());
}

JSValueRef makeStringValue(JSContextRef context, const std::string &value) {
  JSStringRef string = makeString(value);

  if (!string) return JSValueMakeUndefined(context);

  JSValueRef result = JSValueMakeString(context, string);

  JSStringRelease(string);

  return result;
}

void setProperty(JSContextRef context, JSObjectRef object, const char *name, JSValueRef value) {
  if (!object || !name || !value) return;

  JSStringRef property = JSStringCreateWithUTF8CString(name);

  if (!property) return;

  JSObjectSetProperty(context, object, property, value, kJSPropertyAttributeNone, nullptr);

  JSStringRelease(property);
}

JSObjectRef makeBasicError(JSContextRef context, const std::string &type,
                           const std::string &message) {
  JSStringRef messageString = makeString(message);

  if (!messageString) return nullptr;

  JSValueRef arguments[] = {JSValueMakeString(context, messageString)};

  JSObjectRef error = JSObjectMakeError(context, 1, arguments, nullptr);

  JSStringRelease(messageString);

  if (!error) return nullptr;

  JSValueRef name = makeStringValue(context, type);

  if (name) setProperty(context, error, "name", name);

  return error;
}

std::vector<std::string> splitLines(const std::string &source) {
  std::vector<std::string> lines;
  std::size_t start = 0;

  while (start <= source.size()) {
    const std::size_t end = source.find('\n', start);

    if (end == std::string::npos) {
      lines.push_back(source.substr(start));
      break;
    }

    lines.push_back(source.substr(start, end - start));

    start = end + 1;
  }

  return lines;
}

std::size_t visualColumn(const std::string &line, std::size_t column) {
  if (column == 0) return 0;

  const std::size_t characterIndex = std::min(column - 1, line.size());

  std::size_t visual = 0;

  for (std::size_t i = 0; i < characterIndex; ++i) {
    if (line[i] == '\t') visual += 4 - (visual % 4);
    else
      ++visual;
  }

  return visual;
}

std::string makeCaretLine(const std::string &sourceLine, const SourceLocation &location) {
  const std::size_t column = visualColumn(sourceLine, location.column);

  std::size_t width = 1;

  if (location.endColumn > location.column) {
    const std::size_t end = visualColumn(sourceLine, location.endColumn);

    if (end > column) width = end - column;
  }

  std::string result(column, ' ');
  result += '^';

  if (width > 1) result.append(width - 1, '~');

  return result;
}

std::string renderSourceExcerpt(const std::string &source, const SourceLocation &location) {
  if (source.empty() || location.line == 0) return {};

  const std::vector<std::string> lines = splitLines(source);

  const std::size_t lineIndex = location.line - 1;

  if (lineIndex >= lines.size()) return {};

  const std::size_t firstLine = lineIndex > 0 ? lineIndex - 1 : lineIndex;

  const std::size_t lastLine = std::min(lineIndex + 1, lines.size() - 1);

  std::string result;

  for (std::size_t i = firstLine; i <= lastLine; ++i) {
    result += std::to_string(i + 1);
    result += " | ";
    result += lines[i];
    result += '\n';

    if (i == lineIndex) {
      result += "    ";
      result += makeCaretLine(lines[i], location);
      result += '\n';
    }
  }

  return result;
}

void appendLocation(std::string &message, const SourceFrame &frame) {
  message += "\n    at ";

  if (!frame.context.empty()) message += frame.context;
  else
    message += "<anonymous>";

  if (!frame.filename.empty()) {
    message += " (";
    message += frame.filename;

    if (frame.location.line != 0) {
      message += ":";
      message += std::to_string(frame.location.line);
    }

    if (frame.location.column != 0) {
      message += ":";
      message += std::to_string(frame.location.column);
    }

    message += ")";
  } else if (frame.location.line != 0) {
    message += " (";
    message += std::to_string(frame.location.line);

    if (frame.location.column != 0) {
      message += ":";
      message += std::to_string(frame.location.column);
    }

    message += ")";
  }
}

} // namespace

const char *errorType(ErrorCode code) {
  const ErrorDefinition *definition = findDefinition(code);

  return definition ? definition->type : "FFIError";
}

const char *errorTemplate(ErrorCode code) {
  const ErrorDefinition *definition = findDefinition(code);

  return definition ? definition->message : "Internal FFI error";
}

std::string formatError(ErrorCode code) { return errorTemplate(code); }

std::string formatError(ErrorCode code, const std::string &value) {
  return formatString(errorTemplate(code), value);
}

std::string formatError(ErrorCode code, std::size_t value) {
  return formatSize(errorTemplate(code), value);
}

std::string formatError(ErrorCode code, std::size_t first, std::size_t second) {
  return formatSizes(errorTemplate(code), first, second);
}

std::string formatError(ErrorCode code, const std::string &value, std::size_t index) {
  return formatStringAndSize(errorTemplate(code), value, index);
}

std::string formatError(ErrorCode code, const std::string &value, int number) {
  return formatStringAndInt(errorTemplate(code), value, number);
}

JSObjectRef makeError(JSContextRef context, ErrorCode code) {
  return makeBasicError(context, errorType(code), formatError(code));
}

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value) {
  return makeBasicError(context, errorType(code), formatError(code, value));
}

JSObjectRef makeError(JSContextRef context, ErrorCode code, std::size_t value) {
  return makeBasicError(context, errorType(code), formatError(code, value));
}

JSObjectRef makeError(JSContextRef context, ErrorCode code, std::size_t first, std::size_t second) {
  return makeBasicError(context, errorType(code), formatError(code, first, second));
}

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value,
                      std::size_t index) {
  return makeBasicError(context, errorType(code), formatError(code, value, index));
}

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value, int number) {
  return makeBasicError(context, errorType(code), formatError(code, value, number));
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code) {
  if (!error || *error) return;

  *error = makeError(context, code);
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value) {
  if (!error || *error) return;

  *error = makeError(context, code, value);
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, std::size_t value) {
  if (!error || *error) return;

  *error = makeError(context, code, value);
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, std::size_t first,
                std::size_t second) {
  if (!error || *error) return;

  *error = makeError(context, code, first, second);
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value,
                std::size_t index) {
  if (!error || *error) return;

  *error = makeError(context, code, value, index);
}

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value,
                int number) {
  if (!error || *error) return;

  *error = makeError(context, code, value, number);
}

JSObjectRef makeCompilationError(JSContextRef context, const std::string &source,
                                 const SourceDiagnostic &diagnostic) {
  std::string message;

  const std::string excerpt = renderSourceExcerpt(source, diagnostic.location);

  if (!excerpt.empty()) message += excerpt;

  message += errorType(ErrorCode::CompilationFailed);
  message += ": ";
  message += diagnostic.message;

  if (!diagnostic.frames.empty()) {
    for (const SourceFrame &frame : diagnostic.frames) { appendLocation(message, frame); }
  } else {
    SourceFrame frame;

    frame.context = diagnostic.filename.empty() ? "C source" : diagnostic.filename;

    frame.filename = diagnostic.filename;

    frame.location = diagnostic.location;

    appendLocation(message, frame);
  }

  return makeBasicError(context, errorType(ErrorCode::CompilationFailed), message);
}

}} // namespace edon::ffi
