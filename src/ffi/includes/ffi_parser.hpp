#pragma once

#include <string>
#include <vector>
#include <memory>

#include "ffi_types.hpp"

namespace edon {
namespace ffi {

struct ParsedDeclaration {
    std::string name;
    std::shared_ptr<Type> type;
};

struct ParsedFunction {
    std::string name;
    Signature signature;
};

struct ParsedCSource {
    std::vector<ParsedDeclaration> declarations;
    std::vector<ParsedFunction> functions;
};

bool parseType(
    const std::string& source,
    std::shared_ptr<Type>& type,
    std::string& error
);

bool parseFunctionDeclaration(
    const std::string& source,
    ParsedFunction& function,
    std::string& error
);

bool parseCSource(
    const std::string& source,
    ParsedCSource& result,
    std::string& error
);

} // namespace ffi
} // namespace edon
