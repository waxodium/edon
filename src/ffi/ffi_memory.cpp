#include "ffi_memory.hpp"

#include "ffi_errors.hpp"

#include <cstdlib>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>

namespace edon {
namespace ffi {

namespace {

JSClassRef pointerClass = nullptr;

struct ExternalBufferState {
    JSObjectRef buffer = nullptr;
    std::shared_ptr<Allocation> allocation;
};

std::unordered_map<JSObjectRef, ExternalBufferState*> externalBuffers;

void pointerFinalize(JSObjectRef object) {
    auto* state = static_cast<PointerState*>(
        JSObjectGetPrivate(object)
    );

    delete state;
}

JSClassRef getPointerClass() {
    if (pointerClass)
        return pointerClass;

    JSClassDefinition definition = kJSClassDefinitionEmpty;
    definition.className = "NativePointer";
    definition.finalize = pointerFinalize;

    pointerClass = JSClassCreate(&definition);
    return pointerClass;
}

void externalBufferRelease(
    void* bytes,
    void* deallocatorContext
) {
    (void)bytes;

    auto* state = static_cast<ExternalBufferState*>(
        deallocatorContext
    );

    if (!state)
        return;

    if (state->buffer)
        externalBuffers.erase(state->buffer);

    delete state;
}

} // namespace

Allocation::Allocation(std::size_t bytes) {
    if (bytes == 0)
        return;

    data = std::calloc(1, bytes);

    if (data) {
        size = bytes;
        alive = true;
    }
}

Allocation::~Allocation() {
    release();
}

void Allocation::release() {
    if (!data) {
        size = 0;
        alive = false;
        return;
    }

    std::free(data);

    data = nullptr;
    size = 0;
    alive = false;
}

void Allocation::invalidate() {
    alive = false;
}

PointerState::~PointerState() {
    if (context && rootedValue)
        JSValueUnprotect(context, rootedValue);

    rootedValue = nullptr;
    context = nullptr;
}

bool PointerState::valid() const {
    if (allocation)
        return allocation->alive && allocation->data != nullptr;

    return address != 0;
}

void PointerState::invalidate() {
    address = 0;
    allocation.reset();
}

JSObjectRef makeNativePointer(
    JSContextRef context,
    std::uintptr_t address,
    std::shared_ptr<Allocation> allocation,
    JSValueRef rootedValue
) {
    PointerState* state = nullptr;

    try {
        state = new PointerState();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }

    state->address = address;
    state->allocation = std::move(allocation);

    if (rootedValue) {
        state->context = JSContextGetGlobalContext(context);
        state->rootedValue = rootedValue;

        JSValueProtect(context, rootedValue);
    }

    JSObjectRef object = JSObjectMake(
        context,
        getPointerClass(),
        state
    );

    if (!object) {
        delete state;
        return nullptr;
    }

    return object;
}

PointerState* getNativePointer(
    JSContextRef context,
    JSValueRef value
) {
    if (!JSValueIsObject(context, value))
        return nullptr;

    JSObjectRef object = JSValueToObject(
        context,
        value,
        nullptr
    );

    if (!object)
        return nullptr;

    return static_cast<PointerState*>(
        JSObjectGetPrivate(object)
    );
}

JSObjectRef makeExternalArrayBuffer(
    JSContextRef context,
    std::shared_ptr<Allocation> allocation
) {
    if (!allocation ||
        !allocation->alive ||
        !allocation->data)
        return nullptr;

    ExternalBufferState* state = nullptr;

    try {
        state = new ExternalBufferState();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }

    state->allocation = std::move(allocation);

    JSObjectRef buffer = JSObjectMakeArrayBufferWithBytesNoCopy(
        context,
        state->allocation->data,
        state->allocation->size,
        externalBufferRelease,
        state,
        nullptr
    );

    if (!buffer) {
        delete state;
        return nullptr;
    }

    state->buffer = buffer;

    try {
        externalBuffers.emplace(buffer, state);
    } catch (const std::bad_alloc&) {
        /*
         * The ArrayBuffer now owns the deallocator state.
         * Do not delete state here because the ArrayBuffer may
         * invoke externalBufferRelease later.
         */
    }

    return buffer;
}

bool getBufferPointer(
    JSContextRef context,
    JSValueRef value,
    void*& data,
    std::size_t& size,
    JSValueRef* error
) {
    data = nullptr;
    size = 0;

    if (!JSValueIsObject(context, value)) {
        throwError(
            context,
            error,
            ErrorCode::InvalidBuffer
        );
        return false;
    }

    JSValueRef localError = nullptr;

    JSObjectRef object = JSValueToObject(
        context,
        value,
        &localError
    );

    if (localError) {
        if (error && !*error)
            *error = localError;

        return false;
    }

    auto it = externalBuffers.find(object);

    if (it != externalBuffers.end()) {
        ExternalBufferState* state = it->second;

        if (!state ||
            !state->allocation ||
            !state->allocation->alive ||
            !state->allocation->data) {
            throwError(
                context,
                error,
                ErrorCode::FreedMemory
            );
            return false;
        }
    }

    const std::size_t byteLength =
        JSObjectGetArrayBufferByteLength(
            context,
            object,
            &localError
        );

    if (localError) {
        if (error && !*error)
            *error = localError;

        return false;
    }

    void* bytes = JSObjectGetArrayBufferBytesPtr(
        context,
        object,
        &localError
    );

    if (localError) {
        if (error && !*error)
            *error = localError;

        return false;
    }

    data = bytes;
    size = byteLength;

    return true;
}

bool getPointerValue(
    JSContextRef context,
    JSValueRef value,
    std::uintptr_t& address,
    JSValueRef* error
) {
    PointerState* state = getNativePointer(
        context,
        value
    );

    if (!state) {
        throwError(
            context,
            error,
            ErrorCode::ExpectedPointer
        );
        return false;
    }

    if (!state->valid()) {
        throwError(
            context,
            error,
            ErrorCode::InvalidPointer
        );
        return false;
    }

    if (state->allocation)
        address = reinterpret_cast<std::uintptr_t>(
            state->allocation->data
        );
    else
        address = state->address;

    return true;
}

JSValueRef allocateSharedBuffer(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
) {
    if (argumentCount != 1) {
        throwError(
            context,
            error,
            ErrorCode::ArgumentCountMismatch,
            1,
            argumentCount
        );
        return nullptr;
    }

    JSValueRef localError = nullptr;

    const double number = JSValueToNumber(
        context,
        arguments[0],
        &localError
    );

    if (localError) {
        if (error && !*error)
            *error = localError;

        return nullptr;
    }

    const double maximum =
        sizeof(std::size_t) <= 4
            ? static_cast<double>(
                  std::numeric_limits<std::size_t>::max()
              )
            : 9007199254740991.0;

    if (number < 0 ||
        number != number ||
        number > maximum ||
        number != static_cast<double>(
            static_cast<std::size_t>(number)
        )) {
        throwError(
            context,
            error,
            ErrorCode::InvalidBufferSize
        );
        return nullptr;
    }

    const std::size_t size =
        static_cast<std::size_t>(number);

    std::shared_ptr<Allocation> allocation;

    try {
        allocation = std::make_shared<Allocation>(size);
    } catch (const std::bad_alloc&) {
        throwError(
            context,
            error,
            ErrorCode::AllocationFailed
        );
        return nullptr;
    }

    if (size != 0 && !allocation->data) {
        throwError(
            context,
            error,
            ErrorCode::AllocationFailed
        );
        return nullptr;
    }

    if (size == 0) {
        JSObjectRef buffer =
            JSObjectMakeArrayBufferWithBytesNoCopy(
                context,
                nullptr,
                0,
                nullptr,
                nullptr,
                error
            );

        if (!buffer)
            return nullptr;

        return buffer;
    }

    JSObjectRef buffer = makeExternalArrayBuffer(
        context,
        std::move(allocation)
    );

    if (!buffer) {
        throwError(
            context,
            error,
            ErrorCode::ObjectCreationFailed
        );
        return nullptr;
    }

    return buffer;
}

JSValueRef addressOf(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
) {
    if (argumentCount != 1) {
        throwError(
            context,
            error,
            ErrorCode::ArgumentCountMismatch,
            1,
            argumentCount
        );
        return nullptr;
    }

    void* data = nullptr;
    std::size_t size = 0;

    if (!getBufferPointer(
            context,
            arguments[0],
            data,
            size,
            error
        ))
        return nullptr;

    (void)size;

    JSObjectRef buffer = JSValueToObject(
        context,
        arguments[0],
        error
    );

    if (!buffer)
        return nullptr;

    auto it = externalBuffers.find(buffer);

    if (it == externalBuffers.end() ||
        !it->second ||
        !it->second->allocation) {
        throwError(
            context,
            error,
            ErrorCode::NotOwnedMemory
        );
        return nullptr;
    }

    std::shared_ptr<Allocation> allocation =
        it->second->allocation;

    if (!allocation->alive || !allocation->data) {
        throwError(
            context,
            error,
            ErrorCode::FreedMemory
        );
        return nullptr;
    }

    data = allocation->data;

    JSObjectRef pointer = makeNativePointer(
        context,
        reinterpret_cast<std::uintptr_t>(data),
        std::move(allocation),
        arguments[0]
    );

    if (!pointer) {
        throwError(
            context,
            error,
            ErrorCode::ObjectCreationFailed
        );
        return nullptr;
    }

    return pointer;
}

JSValueRef freeNativeMemory(
    JSContextRef context,
    size_t argumentCount,
    const JSValueRef arguments[],
    JSValueRef* error
) {
    if (argumentCount != 1) {
        throwError(
            context,
            error,
            ErrorCode::ArgumentCountMismatch,
            1,
            argumentCount
        );
        return nullptr;
    }

    /*
        NativePointer path.
    */
    PointerState* pointer = getNativePointer(
        context,
        arguments[0]
    );

    if (pointer) {
        if (!pointer->allocation) {
            throwError(
                context,
                error,
                ErrorCode::NotOwnedMemory
            );
            return nullptr;
        }

        if (!pointer->allocation->alive) {
            throwError(
                context,
                error,
                ErrorCode::FreedMemory
            );
            return nullptr;
        }

        pointer->allocation->invalidate();
        pointer->address = 0;

        return JSValueMakeUndefined(context);
    }

    /*
        Managed ArrayBuffer path.
    */
    if (JSValueIsObject(context, arguments[0])) {
        JSObjectRef buffer = JSValueToObject(
            context,
            arguments[0],
            error
        );

        if (!buffer)
            return nullptr;

        auto it = externalBuffers.find(buffer);

        if (it != externalBuffers.end() &&
            it->second &&
            it->second->allocation) {
            std::shared_ptr<Allocation> allocation =
                it->second->allocation;

            if (!allocation->alive) {
                throwError(
                    context,
                    error,
                    ErrorCode::FreedMemory
                );
                return nullptr;
            }

            allocation->invalidate();

            return JSValueMakeUndefined(context);
        }
    }

    throwError(
        context,
        error,
        ErrorCode::FreeInvalidArgument
    );

    return nullptr;
}

} // namespace ffi
} // namespace edon
