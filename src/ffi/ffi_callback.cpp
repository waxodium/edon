#include "ffi_callback.hpp"
#include "ffi_call.hpp"
#include "ffi_errors.hpp"
#include "ffi_memory.hpp"

#include <ffi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace edon {
namespace ffi {

namespace {

JSClassRef callbackClass = nullptr;

void callbackFinalize(JSObjectRef object) {
  auto *state = static_cast<CallbackState *>(JSObjectGetPrivate(object));

  delete state;
}

JSClassRef getCallbackClass() {
  if (callbackClass)
    return callbackClass;

  JSClassDefinition definition = kJSClassDefinitionEmpty;
  definition.className = "NativeCallback";
  definition.finalize = callbackFinalize;

  callbackClass = JSClassCreate(&definition);

  return callbackClass;
}

JSValueRef nativeArgumentToJS(JSContextRef context,
                              const std::shared_ptr<Type> &type, void *value) {
  if (!type || !value)
    return nullptr;

  switch (type->kind) {
  case TypeKind::Void:
    return JSValueMakeUndefined(context);

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
    return JSBigIntCreateWithInt64(
        context, *static_cast<const int64_t *>(value), nullptr);

  case TypeKind::UInt64:
    return JSBigIntCreateWithUInt64(
        context, *static_cast<const uint64_t *>(value), nullptr);

  case TypeKind::Size:
    if (sizeof(std::size_t) == sizeof(uint64_t)) {
      return JSBigIntCreateWithUInt64(
          context,
          static_cast<uint64_t>(*static_cast<const std::size_t *>(value)),
          nullptr);
    }

    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const std::size_t *>(value)));

  case TypeKind::SSize:
    if (sizeof(std::ptrdiff_t) == sizeof(int64_t)) {
      return JSBigIntCreateWithInt64(
          context,
          static_cast<int64_t>(*static_cast<const std::ptrdiff_t *>(value)),
          nullptr);
    }

    return JSValueMakeNumber(
        context,
        static_cast<double>(*static_cast<const std::ptrdiff_t *>(value)));

  case TypeKind::Float:
    return JSValueMakeNumber(
        context, static_cast<double>(*static_cast<const float *>(value)));

  case TypeKind::Double:
    return JSValueMakeNumber(context, *static_cast<const double *>(value));

  case TypeKind::Pointer: {
    void *address = *static_cast<void *const *>(value);

    if (!address)
      return JSValueMakeNull(context);

    return makeNativePointer(context,
                             reinterpret_cast<std::uintptr_t>(address));
  }

  case TypeKind::CString: {
    const char *string = *static_cast<const char *const *>(value);

    if (!string)
      return JSValueMakeNull(context);

    JSStringRef jsString = JSStringCreateWithUTF8CString(string);

    if (!jsString)
      return nullptr;

    JSValueRef result = JSValueMakeString(context, jsString);

    JSStringRelease(jsString);

    return result;
  }

  case TypeKind::Struct:
  case TypeKind::Array:
    return nullptr;
  }

  return nullptr;
}

bool writeCallbackResult(JSContextRef context,
                         const std::shared_ptr<Type> &type, JSValueRef value,
                         void *result) {
  if (!type || type->kind == TypeKind::Void)
    return true;

  if (!value || !result)
    return false;

  JSValueRef error = nullptr;

  if (!jsValueToNative(context, value, type, result, &error))
    return false;

  return error == nullptr;
}

void closureEntry(ffi_cif *cif, void *result, void **arguments,
                  void *userData) {
  (void)cif;

  auto *state = static_cast<CallbackState *>(userData);

  if (!state || !state->alive || state->destroyed || !state->context ||
      !state->function) {
    return;
  }

  if (result && state->signature.returns &&
      state->signature.returns->kind != TypeKind::Void) {
    std::memset(result, 0, state->signature.returns->size);
  }

  JSContextRef context = state->context;
  const Signature &signature = state->signature;

  if (!signature.prepared ||
      signature.args.size() >
          static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
    return;
  }

  std::vector<JSValueRef> jsArguments;

  try {
    jsArguments.reserve(signature.args.size());

    for (std::size_t i = 0; i < signature.args.size(); ++i) {
      if (!arguments || !arguments[i])
        return;

      const std::shared_ptr<Type> &type = signature.args[i];

      if (!type || !type->ffi || !type->complete) {
        return;
      }

      JSValueRef value = nativeArgumentToJS(context, type, arguments[i]);

      if (!value)
        return;

      jsArguments.push_back(value);
    }
  } catch (const std::bad_alloc &) {
    return;
  } catch (...) {
    return;
  }

  JSValueRef callbackError = nullptr;

  JSValueRef returnValue = JSObjectCallAsFunction(
      context, state->function, nullptr, jsArguments.size(),
      jsArguments.empty() ? nullptr : jsArguments.data(), &callbackError);

  if (callbackError || !returnValue)
    return;

  if (!signature.returns || signature.returns->kind == TypeKind::Void) {
    return;
  }

  (void)writeCallbackResult(context, signature.returns, returnValue, result);
}

} // namespace

CallbackState::~CallbackState() { destroy(); }

void CallbackState::destroy() {
  if (destroyed)
    return;

  destroyed = true;
  alive = false;

  if (function && context) {
    JSValueUnprotect(context, function);
    function = nullptr;
  }

  if (closure) {
    ffi_closure_free(closure);
    closure = nullptr;
    executable = nullptr;
  }

  context = nullptr;
}

JSObjectRef createCallback(JSContextRef context, JSObjectRef function,
                           Signature signature, JSValueRef *error) {
  if (!function || !JSObjectIsFunction(context, function)) {
    throwError(context, error, ErrorCode::ExpectedFunction);
    return nullptr;
  }

  if (!signature.prepared && !prepareSignature(signature)) {
    throwError(context, error, ErrorCode::SignaturePreparationFailed);
    return nullptr;
  }

  JSClassRef classRef = getCallbackClass();

  if (!classRef) {
    throwError(context, error, ErrorCode::FunctionCreationFailed);
    return nullptr;
  }

  auto *state = new (std::nothrow) CallbackState();

  if (!state) {
    throwError(context, error, ErrorCode::AllocationFailed);
    return nullptr;
  }

  state->context = JSContextGetGlobalContext(context);
  state->function = function;
  state->signature = std::move(signature);

  JSValueProtect(context, state->function);

  state->closure = ffi_closure_alloc(sizeof(ffi_closure), &state->executable);

  if (!state->closure || !state->executable) {
    if (state->closure)
      ffi_closure_free(state->closure);

    state->closure = nullptr;
    state->executable = nullptr;

    JSValueUnprotect(context, state->function);
    state->function = nullptr;

    delete state;

    throwError(context, error, ErrorCode::AllocationFailed);

    return nullptr;
  }

  const ffi_status status = ffi_prep_closure_loc(
      static_cast<ffi_closure *>(state->closure), &state->signature.cif,
      closureEntry, state, state->executable);

  if (status != FFI_OK) {
    ffi_closure_free(state->closure);

    state->closure = nullptr;
    state->executable = nullptr;

    JSValueUnprotect(context, state->function);
    state->function = nullptr;

    delete state;

    throwError(context, error, ErrorCode::FunctionCreationFailed);

    return nullptr;
  }

  state->alive = true;

  JSObjectRef object = JSObjectMake(context, classRef, state);

  if (!object) {
    state->destroy();
    delete state;

    throwError(context, error, ErrorCode::ObjectCreationFailed);

    return nullptr;
  }

  return object;
}

JSValueRef destroyCallback(JSContextRef context, JSValueRef callback,
                           JSValueRef *error) {
  CallbackState *state = getCallbackState(context, callback);

  if (!state) {
    throwError(context, error, ErrorCode::InvalidArgument);
    return nullptr;
  }

  state->destroy();

  return JSValueMakeUndefined(context);
}

CallbackState *getCallbackState(JSContextRef context, JSValueRef value) {
  if (!JSValueIsObject(context, value))
    return nullptr;

  JSObjectRef object = JSValueToObject(context, value, nullptr);

  if (!object)
    return nullptr;

  if (!callbackClass)
    return nullptr;

  return static_cast<CallbackState *>(JSObjectGetPrivate(object));
}

} // namespace ffi
} // namespace edon
