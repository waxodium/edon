#include <iostream>
#include <vector>
#include <string>
#include "cli.hpp"

extern "C" {
    void edon_init_runtime() {}
}

int main(int argc, char* argv[]) {
    edon_init_runtime();

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::cout << "no inputs were passed\n";
        return 0;
    }

    SplitArgs split = splitCommandArgs(args);
    ParseResult global_parsed = parseOptions(split.global_args, GLOBAL_OPTIONS);

    if (global_parsed.flags["version"] == "true") {
        std::cout << "v" << edon::HEADEDON.version << "\n";
        return 0;
    }

    if (global_parsed.flags["help"] == "true" && split.command_name.empty()) {
        printHelp();
        return 0;
    }

    if (split.command_name == "help") {
        if (!split.command_args.empty()) {
            printHelp(split.command_args[0]);
        } else {
            printHelp();
        }
        return 0;
    }

    std::cerr << "edon: '" << split.command_name << "' is unrecognized.\n";
    std::cerr << "Run 'edon --help' for valid commands.\n";
    return 1;
}
