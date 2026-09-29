#include "ffi_build.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace edon {
namespace ffi {

BuildResult buildNativeSource(const std::string& source, const BuildOptions& options) {
    BuildResult result;

    if (options.outputPath.empty()) {
        result.error =
            "Native build output path is required";

        return result;
    }


    (void)source;
    (void)options;

    // w.i.p

    result.error = "External native build integration is not implemented (w.i.p)";

    return result;
}

} // namespace ffi
} // namespace edon
