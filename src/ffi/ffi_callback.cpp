#include "ffi_callback.hpp"

#include <ffi.h>

/*
 *
 *   This used to not be fine.
 *   But i don't know how it works now. (scary)
 *
 */

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

void closureEntry(ffi_cif *cif, void *result, void **arguments,
                  void *userData) {
  auto *state = static_cast<CallbackState *>(userData);

  if (!state || !state->alive || state->destroyed || !state->context ||
      !state->function)
    return;

  (void)cif;
  (void)result;
  (void)arguments;
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
    if (error) {
      JSStringRef text =
          JSStringCreateWithUTF8CString("createCallback expects a function");

      *error = JSValueMakeString(context, text);
      JSStringRelease(text);
    }

    return nullptr;
  }

  if (!signature.prepared && !prepareSignature(signature)) {
    if (error) {
      JSStringRef text =
          JSStringCreateWithUTF8CString("Invalid callback signature");

      *error = JSValueMakeString(context, text);
      JSStringRelease(text);
    }

    return nullptr;
  }

  auto *state = new CallbackState();

  state->context = JSContextGetGlobalContext(context);
  state->function = function;
  state->signature = std::move(signature);

  JSValueProtect(context, state->function);

  state->closure = ffi_closure_alloc(sizeof(ffi_closure), &state->executable);

  if (!state->closure) {
    JSValueUnprotect(context, state->function);
    state->function = nullptr;
    delete state;

    if (error) {
      JSStringRef text =
          JSStringCreateWithUTF8CString("ffi_closure_alloc failed");

      *error = JSValueMakeString(context, text);
      JSStringRelease(text);
    }

    return nullptr;
  }

  ffi_status status = ffi_prep_closure_loc(
      static_cast<ffi_closure *>(state->closure), &state->signature.cif,
      closureEntry, state, state->executable);

  if (status != FFI_OK) {
    ffi_closure_free(state->closure);

    state->closure = nullptr;
    state->executable = nullptr;

    JSValueUnprotect(context, state->function);
    state->function = nullptr;

    delete state;

    if (error) {
      JSStringRef text =
          JSStringCreateWithUTF8CString("ffi_prep_closure_loc failed");

      *error = JSValueMakeString(context, text);
      JSStringRelease(text);
    }

    return nullptr;
  }

  state->alive = true;

  return JSObjectMake(context, getCallbackClass(), state);
}

JSValueRef destroyCallback(JSContextRef context, JSValueRef callback,
                           JSValueRef *error) {
  CallbackState *state = getCallbackState(context, callback);

  if (!state) {
    if (error) {
      JSStringRef text =
          JSStringCreateWithUTF8CString("Expected NativeCallback");

      *error = JSValueMakeString(context, text);
      JSStringRelease(text);
    }

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

  return static_cast<CallbackState *>(JSObjectGetPrivate(object));
}

} // namespace ffi
} // namespace edon
