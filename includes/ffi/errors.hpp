#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <string>
#include <vector>

namespace edon { namespace ffi {

enum class ErrorCode {
  InvalidArgumentCount,
  ArgumentCountMismatch,
  InvalidArgument,
  InvalidArgumentList,

  InvalidSignature,
  SignaturePreparationFailed,
  MissingReturnType,
  MissingArgumentTypes,
  MissingArgumentType,
  EmptyReturnType,
  EmptyArgumentType,
  InvalidType,
  UnknownReturnType,
  UnknownArgumentType,
  UnsupportedType,
  UnsupportedReturnType,
  UnsupportedArgumentType,

  InvalidNativeModule,
  InvalidNativeFunction,
  EmptyFunctionName,
  FunctionNotFound,
  EmptyModulePath,
  ModuleLoadFailed,
  ModuleNotFound,
  ModuleSystemUnavailable,

  MissingSource,
  InvalidSource,
  EmptySource,
  EmptyTemplate,
  TemplateSubstitution,
  CompilationFailed,

  InvalidPointer,
  InvalidMemoryArgument,
  InvalidBuffer,
  InvalidBufferSize,
  InvalidAllocation,
  AllocationFailed,
  FreedMemory,
  UntrackedMemory,
  AlreadyFreed,
  NotOwnedMemory,
  NullPointer,
  AddressOfInvalidArgument,
  FreeInvalidArgument,

  ExpectedNumber,
  ExpectedString,
  ExpectedObject,
  ExpectedFunction,
  ExpectedPointer,
  InvalidValue,
  InvalidInteger,
  IntegerOutOfRange,
  UnsafeInteger,
  InvalidFloat,
  FloatOverflow,
  InvalidCString,

  CallbackInvalidArgument,
  CallbackAlreadyDestroyed,
  CallbackCreationFailed,

  NativeCallFailed,
  NativeCrash,
  NativeSegmentationFault,

  ObjectCreationFailed,
  PropertyCreationFailed,
  FunctionCreationFailed,
  StateCreationFailed,
  InternalError
};

struct SourceLocation {
  std::size_t line = 0;
  std::size_t column = 0;
  std::size_t endColumn = 0;
};

struct SourceFrame {
  std::string context;
  std::string filename;
  SourceLocation location;
};

struct SourceDiagnostic {
  std::string filename;
  std::string message;
  SourceLocation location;
  std::vector<SourceFrame> frames;
};

const char *errorType(ErrorCode code);

const char *errorTemplate(ErrorCode code);

std::string formatError(ErrorCode code);

std::string formatError(ErrorCode code, const std::string &value);

std::string formatError(ErrorCode code, std::size_t value);

std::string formatError(ErrorCode code, std::size_t first, std::size_t second);

std::string formatError(ErrorCode code, const std::string &value, std::size_t index);

std::string formatError(ErrorCode code, const std::string &value, int number);

JSObjectRef makeError(JSContextRef context, ErrorCode code);

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value);

JSObjectRef makeError(JSContextRef context, ErrorCode code, std::size_t value);

JSObjectRef makeError(JSContextRef context, ErrorCode code, std::size_t first, std::size_t second);

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value,
                      std::size_t index);

JSObjectRef makeError(JSContextRef context, ErrorCode code, const std::string &value, int number);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, std::size_t value);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, std::size_t first,
                std::size_t second);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value,
                std::size_t index);

void throwError(JSContextRef context, JSValueRef *error, ErrorCode code, const std::string &value,
                int number);

JSObjectRef makeCompilationError(JSContextRef context, const std::string &source,
                                 const SourceDiagnostic &diagnostic);

}} // namespace edon::ffi
