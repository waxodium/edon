#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <iomanip>
#include <vector>
#include "edon.hpp"
#include "parsercli.hpp"

namespace color {
    inline constexpr std::string_view RESET          = "\033[0m";
    inline constexpr std::string_view UNDERLINE      = "\033[4m";
    inline constexpr std::string_view BOLD           = "\033[1m";

    inline constexpr std::string_view RED            = "\033[38;2;251;73;52m";   // #fb4934 (Bright Red)
    inline constexpr std::string_view GREEN          = "\033[38;2;184;187;38m";  // #b8bb26 (Bright Green)
    inline constexpr std::string_view YELLOW         = "\033[38;2;250;189;47m";  // #fabd2f (Bright Yellow)
    inline constexpr std::string_view BLUE           = "\033[38;2;131;165;152m"; // #83a598 (Bright Blue)
    inline constexpr std::string_view ORANGE         = "\033[38;2;254;128;25m";  // #fe8019 (Bright Orange)
    inline constexpr std::string_view AQUA           = "\033[38;2;142;192;124m"; // #8ec07c (Bright Aqua / Darker Cyan)
    inline constexpr std::string_view GRAY           = "\033[38;2;146;131;116m"; // #928374 (Gray / Muted text)
    
    inline constexpr std::string_view AQUA_BOLD      = "\033[1;38;2;142;192;124m";
    inline constexpr std::string_view YELLOW_BOLD    = "\033[1;38;2;250;189;47m";
}

struct CommandConfig {
    std::string_view name;
    std::string_view description;
    std::string_view usage;
    std::vector<OptionConfig> options = {};
};

inline const std::vector<OptionConfig> GLOBAL_OPTIONS = {
    {.key = "help", .type = OptionType::Bool, .alias = "h", .default_val = "false", .description = "Show help information"},
    {.key = "version", .type = OptionType::Bool, .alias = "v", .default_val = "false", .description = "Display edon version"}
};

inline const std::vector<CommandConfig> REGISTERED_COMMANDS = {
    {.name = "help", .description = "Show detailed help for a specific command", .usage = "edon help [<command>]"},
    {.name = "run", .description = "Execute a file with edon", .usage = "edon run ./script.js\n  edon run ./script.ts" }
};

inline std::string formatFlag(const OptionConfig& opt) {
    std::string flag;
    if (opt.alias.empty()) {
        flag = "  --";
    } else {
        flag = "-" + std::string(opt.alias) + ", --";
    }
    flag += opt.key;
    if (!opt.metavar.empty()) {
        flag += "=" + std::string(opt.metavar);
    }
    return flag;
}


inline void printHelp(std::string_view target_cmd = "") {
    if (!target_cmd.empty()) {
        const CommandConfig* found = nullptr;

        for (const auto& cmd : REGISTERED_COMMANDS) {
            if (cmd.name == target_cmd) {
                found = &cmd;
                break;
            }
        }

        if (!found) {
            std::cerr << "edon: Unknown command '" << target_cmd << "' for help.\n";
            exit(1);
        }

        std::cout << color::UNDERLINE << color::BLUE << "COMMAND:" << color::RESET
                  << "\n  "
                  << color::YELLOW_BOLD << found->name << color::RESET
                  << " - " << found->description << "\n\n";

        std::cout << color::UNDERLINE << color::BLUE << "USAGE:" << color::RESET
                  << "\n  "
                  << found->usage
                  << "\n\n";

        if (!found->options.empty()) {
            std::cout << color::UNDERLINE << color::BLUE
                      << "OPTIONS" << color::RESET << ":\n";

            for (const auto& opt : found->options) {
                std::cout << "  "
                          << color::GREEN
                          << std::left
                          << std::setw(24)
                          << formatFlag(opt)
                          << color::RESET
                          << opt.description
                          << "\n";
            }

            std::cout << "\n";
        }

        return;
    }

    std::cout << edon::HEADEDON.description << "\n\n";

    std::cout << color::UNDERLINE << color::BLUE << "Usage" << color::RESET << ":\n  "
              << color::ORANGE
              << edon::HEADEDON.name
              << color::RESET
              << " [globals] <command> [options] [...args]\n\n";

    if (!GLOBAL_OPTIONS.empty()) {
        std::cout << color::UNDERLINE << color::BLUE
                  << "GLOBAL OPTIONS" << color::RESET << ":\n";

        for (const auto& opt : GLOBAL_OPTIONS) {
            std::cout << "  "
                      << color::GREEN
                      << std::left
                      << std::setw(24)
                      << formatFlag(opt)
                      << color::RESET
                      << opt.description
                      << "\n";
        }

        std::cout << "\n";
    }

    if (!REGISTERED_COMMANDS.empty()) {
        std::cout << color::UNDERLINE << color::BLUE
                  << "Commands" << color::RESET << ":\n";

        for (const auto& cmd : REGISTERED_COMMANDS) {
            std::cout << "  "
                      << color::AQUA_BOLD
                      << std::left
                      << std::setw(24)
                      << cmd.name
                      << color::RESET
                      << cmd.description
                      << "\n";
        }

        std::cout << "\n";
    }

    std::cout << color::GRAY
              << edon::HEADEDON.name << " "
              << edon::HEADEDON.version
              << " (" << edon::HEADEDON.commit << ")\n"
              << color::RESET;

    std::cout << "Home Page: <"
              << color::UNDERLINE
              << color::AQUA
              << edon::HEADEDON.homepage
              << color::RESET
              << ">\n";
}
