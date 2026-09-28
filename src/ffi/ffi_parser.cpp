#include "ffi_parser.hpp"

#include <cctype>
#include <sstream>

namespace edon {
namespace ffi {

namespace {

std::string trim(const std::string &value) {
  std::size_t first = 0;

  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }

  std::size_t last = value.size();

  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }

  return value.substr(first, last - first);
}

bool splitFunction(const std::string &source, std::string &returnType,
                   std::string &name, std::string &arguments) {
  const std::size_t open = source.find('(');

  const std::size_t close = source.rfind(')');

  if (open == std::string::npos || close == std::string::npos || close < open) {
    return false;
  }

  const std::string prefix = trim(source.substr(0, open));

  const std::size_t separator = prefix.find_last_of(" \t");

  if (separator == std::string::npos)
    return false;

  returnType = trim(prefix.substr(0, separator));

  name = trim(prefix.substr(separator + 1));

  arguments = trim(source.substr(open + 1, close - open - 1));

  return !returnType.empty() && !name.empty();
}

} // namespace

bool parseType(const std::string &source, std::shared_ptr<Type> &type,
               std::string &error) {
  type = parseTypeName(trim(source));

  if (!type) {
    error = "Unsupported C type: " + trim(source);

    return false;
  }

  return true;
}

bool parseFunctionDeclaration(const std::string &source,
                              ParsedFunction &function, std::string &error) {
  std::string returnType;
  std::string name;
  std::string argumentList;

  if (!splitFunction(trim(source), returnType, name, argumentList)) {
    error = "Invalid C function declaration: " + source;

    return false;
  }

  std::shared_ptr<Type> resultType;

  if (!parseType(returnType, resultType, error)) {
    return false;
  }

  Signature signature;
  signature.returns = resultType;

  if (!argumentList.empty() && argumentList != "void") {
    std::stringstream stream(argumentList);
    std::string argument;

    while (std::getline(stream, argument, ',')) {
      argument = trim(argument);

      const std::size_t separator = argument.find_last_of(" \t");

      if (separator != std::string::npos) {
        const std::string candidate = trim(argument.substr(0, separator));

        if (parseTypeName(candidate))
          argument = candidate;
      }

      std::shared_ptr<Type> argumentType;

      if (!parseType(argument, argumentType, error)) {
        return false;
      }

      signature.args.push_back(std::move(argumentType));
    }
  }

  if (!prepareSignature(signature)) {
    error = "Unable to prepare function signature: " + name;

    return false;
  }

  function.name = std::move(name);
  function.signature = std::move(signature);

  return true;
}

bool parseCSource(const std::string &source, ParsedCSource &result,
                  std::string &error) {
  result = {};

  std::stringstream stream(source);
  std::string declaration;

  while (std::getline(stream, declaration, ';')) {
    declaration = trim(declaration);

    if (declaration.empty())
      continue;

    if (declaration.find('(') != std::string::npos) {
      ParsedFunction function;

      if (!parseFunctionDeclaration(declaration, function, error)) {
        return false;
      }

      result.functions.push_back(std::move(function));
    }
  }

  return true;
}

} // namespace ffi
} // namespace edon
