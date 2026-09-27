#include "ffi_build.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace edon {
namespace ffi {

BuildResult buildNativeSource(
    const std::string& source,
    const BuildOptions& options
) {
    BuildResult result;

    if (options.outputPath.empty()) {
        result.error =
            "Native build output path is required";

        return result;
    }

    /*
     * Native source compilation 
     *
     *  Ahh, documentation? Later.
     *
     *  It is optional to use 
     *  agent for assistance to scrape
     *  this git repository.
     */
    (void)source;
    (void)options;

    result.error =
        "External native build integration is not implemented";

    return result;
}

} // namespace ffi
} // namespace edon
