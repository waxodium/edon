#pragma once
#include <string_view>

namespace edon {

#ifndef EDON_COMMIT_HASH
#define EDON_COMMIT_HASH "unknown"
#endif

struct PackageMeta {
    std::string_view name        = "edon";
    std::string_view version     = "0.0.1";
    std::string_view commit      = EDON_COMMIT_HASH;
    std::string_view description = "A minimalist, native-oriented JavaScript runtime.";
    std::string_view homepage    = "https://github.com/waxodium/edon";
    std::string_view license     = "MIT";
};

inline constexpr PackageMeta HEADEDON{};

}
