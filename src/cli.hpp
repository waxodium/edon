#pragma once
#include <vector>
#include <string>
#include <string_view>
#include "parsercli.hpp"
#include "help.hpp"

struct SplitArgs {
    std::vector<std::string> global_args;
    std::string command_name;
    std::vector<std::string> command_args;
};

//stuff vro
inline SplitArgs splitCommandArgs(const std::vector<std::string>& args) {
    int cmd_idx = -1;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (!arg.empty() && arg[0] == '-') {
            if (arg.find('=') != std::string::npos) continue;

            size_t start = arg.find_first_not_of('-');
            std::string_view opt_name = (start == std::string::npos) ? "" : std::string_view(arg).substr(start);

            bool is_string_opt = false;
            for (const auto& opt : GLOBAL_OPTIONS) {
                if (opt_name == opt.key || opt_name == opt.alias) {
                    if (opt.type == OptionType::String) is_string_opt = true;
                    break;
                }
            }

            if (is_string_opt) i++;
            continue;
        }

        cmd_idx = static_cast<int>(i);
        break;
    }

    if (cmd_idx == -1) {
        return { .global_args = args, .command_name = "", .command_args = {} };
    }

    return {
        .global_args = std::vector<std::string>(args.begin(), args.begin() + cmd_idx),
        .command_name = args[cmd_idx],
        .command_args = std::vector<std::string>(args.begin() + cmd_idx + 1, args.end())
    };
}
