#pragma once
#include <iostream>
#include <string_view>

namespace logger {

namespace color {
    inline constexpr std::string_view RESET       = "\033[0m";
    inline constexpr std::string_view CYAN        = "\033[36m";
    inline constexpr std::string_view BOLD_YELLOW = "\033[1;33m";
    inline constexpr std::string_view PURE_RED    = "\033[38;2;255;0;0m";
}

// \cyan\[i]: <text>
inline void info(std::string_view text) {
    std::cout << color::CYAN << "[i]" << color::RESET << ": " << text << "\n";
}

// \yellow\[warning]: <text>
inline void warning(std::string_view text) {
    std::cout << color::BOLD_YELLOW << "[warning]" << color::RESET << ": " << text << "\n";
}

// \red\[error]: <text>
inline void error(std::string_view text) {
    std::cerr << color::PURE_RED << "[error]" << color::RESET << ": " << text << "\n";
}

} 
