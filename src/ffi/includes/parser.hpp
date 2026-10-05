#pragma once

#include "types.hpp"
#include "signature.hpp"

#include <memory>
#include <string>
#include <vector>

namespace edon { namespace ffi {

struct ParsedFunction {
  std::string name;
  Signature signature;
};

struct ParsedCSource {
  std::vector<ParsedFunction> functions;
};

bool parseType(const std::string &source, std::shared_ptr<Type> &type, std::string &error);

bool parseFunctionDeclaration(const std::string &source, ParsedFunction &function,
                              std::string &error);

bool parseCSource(const std::string &source, ParsedCSource &result, std::string &error);

}} // namespace edon::ffi
