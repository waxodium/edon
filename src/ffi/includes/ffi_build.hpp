#pragma once

#include <string>
#include <vector>

namespace edon {
namespace ffi {

struct BuildOptions {
    std::vector<std::string> includePaths;
    std::vector<std::string> libraryPaths;
    std::vector<std::string> libraries;
    std::vector<std::string> compilerArguments;

    std::string outputPath;
};

struct BuildResult {
    bool success = false;
    std::string outputPath;
    std::string error;
};

BuildResult buildNativeSource(
    const std::string& source,
    const BuildOptions& options
);

} // namespace ffi
} // namespace edon
