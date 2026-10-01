#include <iostream>
#include <vector>
#include <string>
#include "cli.hpp"
#include "runtime.hpp"

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        printHelp();
        return 0;
    }

    SplitArgs split = splitCommandArgs(args);
    ParseResult global_parsed = parseOptions(split.global_args, GLOBAL_OPTIONS);

    // Flag: --version
    if (global_parsed.flags["version"] == "true") {
        std::cout << "v" << edon::HEADEDON.version << "\n";
        return 0;
    }

    // Flag: --help
    if (global_parsed.flags["help"] == "true" && split.command_name.empty()) {
        printHelp();
        return 0;
    }

    // Command: help
    if (split.command_name == "help") {
        if (!split.command_args.empty()) {
            printHelp(split.command_args[0]);
        } else {
            printHelp();
        }
        return 0;
    }

    if (split.command_name == "run") {
        if (split.command_args.empty()) {
            std::cerr << "edon run: missing script file\n";
            return 1;
        }

        EdonRuntime runtime;
        if (!runtime.executeFile(split.command_args[0])) {
            return 1;
        }   
        return 0;
    }

    // Default and same as run cmd
    if (!split.command_name.empty()) {
        std::string target_file = split.command_name;

        EdonRuntime runtime;
        if (!runtime.executeFile(target_file)) {
            return 1;
        }
        return 0;
    }

    std::cerr << "edon: '" << split.command_name << "' is unrecognized.\n";
    std::cerr << "Run 'edon --help' for valid commands.\n";
    return 1;
}
