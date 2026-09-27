#pragma once

#include <memory>
#include <string>

namespace edon {
namespace ffi {

struct NativeLibrary {
    void* handle = nullptr;
    std::string path;

    NativeLibrary() = default;
    ~NativeLibrary();

    NativeLibrary(const NativeLibrary&) = delete;
    NativeLibrary& operator=(const NativeLibrary&) = delete;

    void* symbol(const std::string& name) const;

    static std::shared_ptr<NativeLibrary> open(
        const std::string& path,
        std::string& error
    );
};

} // namespace ffi
} // namespace edon
