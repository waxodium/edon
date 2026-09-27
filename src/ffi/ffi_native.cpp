#include "ffi_native.hpp"

#include <dlfcn.h>
#include <libtcc.h>

#include <new>
#include <utility>

namespace edon {
namespace ffi {

NativeModuleState::NativeModuleState(
    NativeModuleKind moduleKind
)
    : kind(moduleKind) {
}

NativeModuleState::~NativeModuleState() {
    if (!handle)
        return;

    if (kind == NativeModuleKind::TCC) {
        tcc_delete(
            static_cast<TCCState*>(
                handle
            )
        );
    } else {
        dlclose(handle);
    }

    handle = nullptr;
}

void* NativeModuleState::resolve(
    const std::string& name
) const {
    if (!handle ||
        name.empty()) {
        return nullptr;
    }

    if (kind == NativeModuleKind::TCC) {
        TCCState* state =
            static_cast<TCCState*>(
                handle
            );

        return tcc_get_symbol(
            state,
            name.c_str()
        );
    }

    dlerror();

    void* address =
        dlsym(
            handle,
            name.c_str()
        );

    return address;
}

std::shared_ptr<NativeModuleState>
NativeModuleState::compile(
    const std::string& source,
    std::string& error
) {
    error.clear();

    if (source.empty()) {
        error =
            "TCC source is empty";

        return nullptr;
    }

    TCCState* state =
        tcc_new();

    if (!state) {
        error =
            "tcc_new failed";

        return nullptr;
    }

    if (tcc_set_output_type(
            state,
            TCC_OUTPUT_MEMORY
        ) < 0) {
        error =
            "TCC output configuration failed";

        tcc_delete(state);

        return nullptr;
    }

    if (tcc_compile_string(
            state,
            source.c_str()
        ) < 0) {
        error =
            "TCC compilation failed";

        tcc_delete(state);

        return nullptr;
    }

    if (tcc_relocate(state) < 0) {
        error =
            "TCC relocation failed";

        tcc_delete(state);

        return nullptr;
    }

    try {
        auto module =
            std::make_shared<
                NativeModuleState
            >(
                NativeModuleKind::TCC
            );

        module->handle =
            state;

        return module;
    } catch (const std::bad_alloc&) {
        error =
            "Native module allocation failed";

        tcc_delete(state);

        return nullptr;
    } catch (...) {
        error =
            "Native module creation failed";

        tcc_delete(state);

        return nullptr;
    }
}

std::shared_ptr<NativeModuleState>
NativeModuleState::load(
    const std::string& path,
    std::string& error
) {
    error.clear();

    if (path.empty()) {
        error =
            "Native library path is empty";

        return nullptr;
    }

    dlerror();

    void* handle =
        dlopen(
            path.c_str(),
            RTLD_NOW | RTLD_LOCAL
        );

    if (!handle) {
        const char* message =
            dlerror();

        error =
            message
                ? message
                : "dlopen failed";

        return nullptr;
    }

    try {
        auto module =
            std::make_shared<
                NativeModuleState
            >(
                NativeModuleKind::DynamicLibrary
            );

        module->handle =
            handle;

        return module;
    } catch (const std::bad_alloc&) {
        error =
            "Native module allocation failed";

        dlclose(handle);

        return nullptr;
    } catch (...) {
        error =
            "Native module creation failed";

        dlclose(handle);

        return nullptr;
    }
}

NativeFunctionState::NativeFunctionState(
    std::shared_ptr<NativeModuleState> nativeModule,
    void* functionAddress,
    std::string functionName
)
    : module(
          std::move(nativeModule)
      ),
      address(functionAddress),
      name(std::move(functionName)) {
}

} // namespace ffi
} // namespace edon
