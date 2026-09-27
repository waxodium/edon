#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace edon {
namespace ffi {

enum class NativeModuleKind {
    TCC,
    DynamicLibrary
};

struct NativeModuleState {
    NativeModuleKind kind;

    void* handle = nullptr;

    NativeModuleState(NativeModuleKind moduleKind);
    ~NativeModuleState();

    NativeModuleState(const NativeModuleState&) = delete;
    NativeModuleState& operator=(const NativeModuleState&) = delete;

    void* resolve(const std::string& name) const;

    static std::shared_ptr<NativeModuleState> compile(
        const std::string& source,
        std::string& error
    );

    static std::shared_ptr<NativeModuleState> load(
        const std::string& path,
        std::string& error
    );
};

struct NativeFunctionState {
    std::shared_ptr<NativeModuleState> module;
    void* address = nullptr;
    std::string name;

    NativeFunctionState(
        std::shared_ptr<NativeModuleState> nativeModule,
        void* functionAddress,
        std::string functionName
    );
};

} // namespace ffi
} // namespace edon
