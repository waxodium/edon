#pragma once

#include "signature.hpp"
#include "types.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edon { namespace ffi {

class TypeContext {
public:
  struct Checkpoint {
    struct TypeSnapshot {
      std::shared_ptr<Type> type;
      std::vector<Field> fields;
      std::vector<EnumValue> enumValues;
      std::size_t size = 0;
      std::size_t alignment = 0;
      bool complete = false;
      ffi_type *ffi = nullptr;
    };

    std::unordered_map<std::string, std::shared_ptr<Type>> structs;
    std::unordered_map<std::string, std::shared_ptr<Type>> unions;
    std::unordered_map<std::string, std::shared_ptr<Type>> enums;
    std::unordered_map<std::string, std::shared_ptr<Type>> typedefs;
    std::vector<TypeSnapshot> types;
  };

  Checkpoint checkpoint() const;
  void rollback(Checkpoint checkpoint);

  std::shared_ptr<Type> findStruct(const std::string &name) const;
  std::shared_ptr<Type> getOrCreateStruct(const std::string &name);
  bool defineStruct(const std::string &name, std::vector<Field> fields);
  void discardIncompleteStruct(const std::string &name);

  std::shared_ptr<Type> findUnion(const std::string &name) const;
  std::shared_ptr<Type> getOrCreateUnion(const std::string &name);
  bool defineUnion(const std::string &name, std::vector<Field> fields);
  void discardIncompleteUnion(const std::string &name);

  std::shared_ptr<Type> findEnum(const std::string &name) const;
  std::shared_ptr<Type> getOrCreateEnum(const std::string &name);
  bool defineEnum(const std::string &name, std::vector<EnumValue> values);
  void discardIncompleteEnum(const std::string &name);

  std::shared_ptr<Type> findTypedef(const std::string &name) const;
  bool defineTypedef(const std::string &name, std::shared_ptr<Type> type);
  bool defineTypedefs(std::vector<std::pair<std::string, std::shared_ptr<Type>>> aliases);

private:
  std::unordered_map<std::string, std::shared_ptr<Type>> structs_;
  std::unordered_map<std::string, std::shared_ptr<Type>> unions_;
  std::unordered_map<std::string, std::shared_ptr<Type>> enums_;
  std::unordered_map<std::string, std::shared_ptr<Type>> typedefs_;
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
