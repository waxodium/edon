#pragma once

#include "signature.hpp"
#include "types.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace edon { namespace ffi {

class TypeContext {
public:
  std::shared_ptr<Type> findStruct(const std::string &name) const;

  std::shared_ptr<Type> getOrCreateStruct(const std::string &name);

  bool defineStruct(const std::string &name, std::vector<Field> fields);

private:
  std::unordered_map<std::string, std::shared_ptr<Type>> structs_;
};

struct ParsedFunction {
  std::string name;
  Signature signature;
};

struct ParsedCSource {
  std::vector<ParsedFunction> functions;
  std::shared_ptr<TypeContext> context;
};

bool parseType(const std::string &source, std::shared_ptr<Type> &type, std::string &error,
               TypeContext *context = nullptr);

bool parseFunctionDeclaration(const std::string &source, ParsedFunction &function,
                              std::string &error);

bool parseCSource(const std::string &source, ParsedCSource &result, std::string &error);

}} // namespace edon::ffi
