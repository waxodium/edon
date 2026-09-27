#pragma once

#include <JavaScriptCore/JavaScript.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace edon {
namespace ffi {

struct Allocation {
    void* data = nullptr;
    std::size_t size = 0;
    bool alive = false;

    Allocation() = default;
    explicit Allocation(std::size_t bytes);
    ~Allocation();

    Allocation(const Allocation&) = delete;
    Allocation& operator=(const Allocation&) = delete;

    void release();
    void invalidate();
};

struct PointerState {
    std::uintptr_t address = 0;

    // If non-null, the pointer keeps its backing allocation alive.
    std::shared_ptr<Allocation> allocation;

    // Optional JS object rooted by the pointer.
    JSGlobalContextRef context = nullptr;
    JSValueRef rootedValue = nullptr;

    PointerState() = default;
    ~PointerState();

    PointerState(const PointerState&) = delete;
    PointerState& operator=(const PointerState&) = delete;

    bool valid() const;
    void invalidate();
};

JSObjectRef makeNativePointer(
    JSContextRef context,
    std::uintptr_t address,
    std::shared_ptr<Allocation> allocation = nullptr,
    JSValueRef rootedValue = nullptr
);

PointerState* getNativePointer(
    JSContextRef context,
    JSValueRef value
);

JSObjectRef makeExternalArrayBuffer(
    JSContextRef context,
    std::shared_ptr<Allocation> allocation
);

bool getBufferPointer(
    JSContextRef context,
    JSValueRef value,
    void*& data,
    std::size_t& size,
    JSValueRef* error
);

bool getPointerValue(
    JSContextRef context,
    JSValueRef value,
    std::uintptr_t& address,
    JSValueRef* error
);

JSValueRef allocateSharedBuffer(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
);

JSValueRef addressOf(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
);

JSValueRef freeNativeMemory(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
);

} // namespace ffi
} // namespace edon
