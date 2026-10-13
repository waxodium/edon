#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace edon { namespace ffi {

struct Allocation {
  explicit Allocation(std::size_t bytes);
  ~Allocation();

  Allocation(const Allocation &) = delete;
  Allocation &operator=(const Allocation &) = delete;

  void release();
  void invalidate();

  void *data = nullptr;
  std::size_t size = 0;
  bool alive = false;
};

struct PointerState {
  std::shared_ptr<Allocation> allocation;

  std::uintptr_t address = 0;

  JSContextRef context = nullptr;
  JSValueRef rootedValue = nullptr;

  ~PointerState();

  bool valid() const;
  void invalidate();
};

JSObjectRef makeNativePointer(JSContextRef context, std::uintptr_t address,
                              std::shared_ptr<Allocation> allocation = nullptr,
                              JSValueRef rootedValue = nullptr);

PointerState *getNativePointer(JSContextRef context, JSValueRef value);

JSObjectRef makeExternalArrayBuffer(JSContextRef context, std::shared_ptr<Allocation> allocation);

bool getBufferPointer(JSContextRef context, JSValueRef value, void *&data, std::size_t &size,
                      JSValueRef *error);

bool getPointerValue(JSContextRef context, JSValueRef value, std::uintptr_t &address,
                     JSValueRef *error);

JSValueRef allocateSharedBuffer(JSContextRef context, std::size_t argumentCount,
                                const JSValueRef arguments[], JSValueRef *error);

JSValueRef addressOf(JSContextRef context, std::size_t argumentCount, const JSValueRef arguments[],
                     JSValueRef *error);

JSValueRef freeNativeMemory(JSContextRef context, std::size_t argumentCount,
                            const JSValueRef arguments[], JSValueRef *error);

}} // namespace edon::ffi
