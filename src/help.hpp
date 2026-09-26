#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <iomanip>
#include "edon.hpp"
#include "parser.hpp"

namespace color {
    inline constexpr std::string_view RESET     = "\033[0m";
    inline constexpr std::string_view UNDERLINE = "\033[4m";
    inline constexpr std::string_view RED       = "\033[38;2;251;73;52m";
    inline constexpr std::string_view GREEN     = "\033[38;2;184;187;38m";
    inline constexpr std::string_view CYAN      = "\033[38;2;142;192;124m";
    inline constexpr std::string_view PINK_BOLD = "\033[1;38;2;211;134;155m";

    inline constexpr std::string_view ORANGE    = "\033[38;2;254;128;25m";
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
    {.name = "help", .description = "Show detailed help for a specific command", .usage = "edon help [command]"}
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

        std::cout << color::UNDERLINE << "COMMAND:" << color::RESET << "\n  "
                  << color::PINK_BOLD << found->name << color::RESET << " - " << found->description << "\n\n";
        std::cout << color::UNDERLINE << "USAGE:" << color::RESET << "\n  "
                  << color::RED << "edon" << color::RESET << " " << found->name << " [options]\n\n";
        return;
    }

    std::cout << edon::HEADEDON.description << "\n\n";
    std::cout << color::UNDERLINE << "Usage" << color::RESET << ":\n  "
              << color::ORANGE << edon::HEADEDON.name << color::RESET << " [globals] <command> [options] [...args]\n\n";

    if (!GLOBAL_OPTIONS.empty()) {
        std::cout << color::UNDERLINE << "GLOBAL OPTIONS" << color::RESET << ":\n";
        for (const auto& opt : GLOBAL_OPTIONS) {
            std::cout << "  " << color::GREEN << std::left << std::setw(24) << formatFlag(opt)
                      << color::RESET << opt.description << "\n";
        }
        std::cout << "\n";
    }

    if (!REGISTERED_COMMANDS.empty()) {
        std::cout << color::UNDERLINE << "Commands" << color::RESET << ":\n";
        for (const auto& cmd : REGISTERED_COMMANDS) {
            std::cout << "  " << color::PINK_BOLD << std::left << std::setw(24) << cmd.name
                      << color::RESET << cmd.description << "\n";
        }
        std::cout << "\n";
    }

    std::cout << edon::HEADEDON.name << " " << edon::HEADEDON.version << " (" << edon::HEADEDON.commit << ")\n";
    std::cout << "Home Page: <" << color::UNDERLINE << color::CYAN << edon::HEADEDON.homepage << color::RESET << ">\n";
}
