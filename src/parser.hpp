#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <iostream>

enum class OptionType { Bool, String };

struct OptionConfig {
    std::string_view key;
    OptionType type;
    std::string_view alias = "";
    std::string_view default_val = "";
    std::string_view description = "";
    std::string_view metavar = "";
};

struct ParseResult {
    std::unordered_map<std::string, std::string> flags;
    std::vector<std::string> remaining;
};



inline bool matchOption(std::string_view arg, const OptionConfig& opt) {
    if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
        return arg.substr(2) == opt.key;
    }
    if (arg.size() == 2 && arg[0] == '-' && !opt.alias.empty()) {
        return arg.substr(1) == opt.alias;
    }
    return false;
}



inline ParseResult parseOptions(const std::vector<std::string>& args, const std::vector<OptionConfig>& schema) {
    ParseResult result;

    for (const auto& opt : schema) {
        result.flags.emplace(opt.key, opt.default_val);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg.empty() || arg[0] != '-') {
            result.remaining.push_back(arg);
            continue;
        }

        std::string_view name = arg;
        std::string_view inline_val;
        bool has_inline = false;

        size_t eq_idx = arg.find('=');
        if (eq_idx != std::string::npos) {
            name = std::string_view(arg).substr(0, eq_idx);
            inline_val = std::string_view(arg).substr(eq_idx + 1);
            has_inline = true;
        }

        const OptionConfig* matched = nullptr;

        for (const auto& opt : schema) {
            if (matchOption(name, opt)) {
                matched = &opt;
                break;
            }
        }

        if (!matched) {
            std::cerr << "edon: Unknown option '" << arg << "'.\n";
            exit(1);
        }

        std::string key_str(matched->key);

        if (matched->type == OptionType::Bool) {
            result.flags[key_str] = "true";
            continue;
        }

        if (matched->type == OptionType::String) {
            if (has_inline) {
                result.flags[key_str] = inline_val;
                continue;
            }

            if (i + 1 < args.size() && (args[i + 1].empty() || args[i + 1][0] != '-')) {
                result.flags[key_str] = args[++i];
                continue;
            }

            std::cerr << "edon: Option '" << arg << "' requires a value.\n";
            exit(1);
        }
    }

    return result;
}

