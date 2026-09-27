#include "ffi_library.hpp"

#include <dlfcn.h>

namespace edon {
namespace ffi {

NativeLibrary::~NativeLibrary() {
    if (!handle)
        return;

    dlclose(handle);
    handle = nullptr;
}

void* NativeLibrary::symbol(const std::string& name) const {
    if (!handle)
        return nullptr;

    dlerror();
    return dlsym(handle, name.c_str());
}

std::shared_ptr<NativeLibrary> NativeLibrary::open(
    const std::string& path,
    std::string& error
) {
    dlerror();

    void* handle = dlopen(
        path.c_str(),
        RTLD_NOW | RTLD_LOCAL
    );

    if (!handle) {
        const char* message = dlerror();
        error = message ? message : "dlopen failed";
        return nullptr;
    }

    auto library = std::make_shared<NativeLibrary>();
    library->handle = handle;
    library->path = path;

    return library;
}

} // namespace ffi
} // namespace edon
