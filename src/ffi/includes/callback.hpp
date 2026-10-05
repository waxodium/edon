#pragma once

#include "signature.hpp"

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <memory>

namespace edon { namespace ffi {

struct CallbackState {
  JSContextRef context = nullptr;
  JSObjectRef function = nullptr;

  Signature signature;

  void *closure = nullptr;
  void *executable = nullptr;

  bool alive = false;
  bool destroyed = false;

  ~CallbackState();

  void destroy();
};

JSObjectRef createCallback(JSContextRef context, JSObjectRef function, Signature signature,
                           JSValueRef *error);

JSValueRef destroyCallback(JSContextRef context, JSValueRef callback, JSValueRef *error);

CallbackState *getCallbackState(JSContextRef context, JSValueRef value);

}} // namespace edon::ffi
