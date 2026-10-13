#include "ffi/signature.hpp"
#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace edon { namespace ffi {

namespace {

template <class Registry>
std::shared_ptr<Type> findNamedType(const Registry &registry, const std::string &name) {
  const auto it = registry.find(name);
  return it == registry.end() ? nullptr : it->second;
}

template <class Registry, class Factory>
std::shared_ptr<Type> getOrCreateNamedType(Registry &registry, const std::string &name,
                                           Factory factory) {
  if (auto type = findNamedType(registry, name)) { return type; }
  auto type = factory(name);
  if (type) { registry.emplace(name, type); }
  return type;
}

template <class Registry, class Values, class Factory>
bool defineNamedType(Registry &registry, const std::string &name, Values values, Factory factory) {
  auto type = getOrCreateNamedType(registry, name,
                                   [&](const std::string &tag) { return factory(tag, Values{}); });
  if (!type || type->complete) { return false; }

  auto candidate = factory(name, std::move(values));
  if (!candidate || !prepareType(candidate)) { return false; }

  type->fields = std::move(candidate->fields);
  type->enumValues = std::move(candidate->enumValues);
  type->size = candidate->size;
  type->alignment = candidate->alignment;
  type->complete = candidate->complete;
  type->ffi = candidate->ffi;
  candidate->ffi = nullptr;
  return true;
}

template <class Registry>
void discardIncompleteNamedType(Registry &registry, const std::string &name) {
  const auto it = registry.find(name);
  if (it != registry.end() && it->second && !it->second->complete) { registry.erase(it); }
}

} // namespace

TypeContext::Checkpoint TypeContext::checkpoint() const {
  Checkpoint result;
  result.structs = structs_;
  result.unions = unions_;
  result.enums = enums_;
  result.typedefs = typedefs_;

  std::unordered_set<const Type *> seenTypes;
  std::unordered_set<const Signature *> seenSignatures;

  const auto snapshotType = [&](const auto &self, const std::shared_ptr<Type> &type) -> void {
    if (!type || !seenTypes.insert(type.get()).second) { return; }

    result.types.push_back(Checkpoint::TypeSnapshot{type, type->fields, type->enumValues,
                                                    type->size, type->alignment, type->complete,
                                                    type->ffi});

    for (const Field &field : type->fields) { self(self, field.type); }

    self(self, type->element);

    if (type->functionSignature) {
      const auto &signature = type->functionSignature;

      if (seenSignatures.insert(signature.get()).second) {
        result.signatures.push_back(Checkpoint::SignatureSnapshot{
            signature, signature->cif, signature->ffiArgs, signature->prepared});
      }

      self(self, signature->returns);

      for (const auto &argument : signature->args) { self(self, argument); }
    }
  };

  const auto snapshotRegistry = [&](const auto &registry) {
    for (const auto &[name, type] : registry) {
      (void)name;
      snapshotType(snapshotType, type);
    }
  };

  snapshotRegistry(structs_);
  snapshotRegistry(unions_);
  snapshotRegistry(enums_);
  snapshotRegistry(typedefs_);

  return result;
}

void TypeContext::rollback(Checkpoint checkpoint) {
  for (auto &snapshot : checkpoint.signatures) {
    if (!snapshot.signature) { continue; }

    snapshot.signature->cif = snapshot.cif;
    snapshot.signature->ffiArgs = std::move(snapshot.ffiArgs);
    snapshot.signature->prepared = snapshot.prepared;
  }

  for (auto &snapshot : checkpoint.types) {
    if (!snapshot.type) { continue; }

    Type &type = *snapshot.type;

    if (type.ffi != snapshot.ffi && type.ffi &&
        (type.kind == TypeKind::Struct || type.kind == TypeKind::Array)) {
      delete[] type.ffi->elements;
      delete type.ffi;
    }

    type.fields = std::move(snapshot.fields);
    type.enumValues = std::move(snapshot.enumValues);
    type.size = snapshot.size;
    type.alignment = snapshot.alignment;
    type.complete = snapshot.complete;
    type.ffi = snapshot.ffi;
  }

  structs_.swap(checkpoint.structs);
  unions_.swap(checkpoint.unions);
  enums_.swap(checkpoint.enums);
  typedefs_.swap(checkpoint.typedefs);
}

std::shared_ptr<Type> TypeContext::findStruct(const std::string &name) const {
  return findNamedType(structs_, name);
}

std::shared_ptr<Type> TypeContext::getOrCreateStruct(const std::string &name) {
  return getOrCreateNamedType(structs_, name,
                              [](const std::string &tag) { return makeStruct(tag, {}); });
}

bool TypeContext::defineStruct(const std::string &name, std::vector<Field> fields) {
  return defineNamedType(structs_, name, std::move(fields),
                         [](const std::string &tag, std::vector<Field> value) {
                           return makeStruct(tag, std::move(value));
                         });
}

void TypeContext::discardIncompleteStruct(const std::string &name) {
  discardIncompleteNamedType(structs_, name);
}

std::shared_ptr<Type> TypeContext::findUnion(const std::string &name) const {
  return findNamedType(unions_, name);
}

std::shared_ptr<Type> TypeContext::getOrCreateUnion(const std::string &name) {
  return getOrCreateNamedType(unions_, name,
                              [](const std::string &tag) { return makeUnion(tag, {}); });
}

bool TypeContext::defineUnion(const std::string &name, std::vector<Field> fields) {
  return defineNamedType(unions_, name, std::move(fields),
                         [](const std::string &tag, std::vector<Field> value) {
                           return makeUnion(tag, std::move(value));
                         });
}

void TypeContext::discardIncompleteUnion(const std::string &name) {
  discardIncompleteNamedType(unions_, name);
}

std::shared_ptr<Type> TypeContext::findEnum(const std::string &name) const {
  return findNamedType(enums_, name);
}

std::shared_ptr<Type> TypeContext::getOrCreateEnum(const std::string &name) {
  return getOrCreateNamedType(enums_, name,
                              [](const std::string &tag) { return makeEnum(tag, {}); });
}

bool TypeContext::defineEnum(const std::string &name, std::vector<EnumValue> values) {
  return defineNamedType(enums_, name, std::move(values),
                         [](const std::string &tag, std::vector<EnumValue> value) {
                           return makeEnum(tag, std::move(value));
                         });
}

void TypeContext::discardIncompleteEnum(const std::string &name) {
  discardIncompleteNamedType(enums_, name);
}

std::shared_ptr<Type> TypeContext::findTypedef(const std::string &name) const {
  const auto it = typedefs_.find(name);
  return it == typedefs_.end() ? nullptr : it->second;
}

bool TypeContext::defineTypedef(const std::string &name, std::shared_ptr<Type> type) {
  std::vector<std::pair<std::string, std::shared_ptr<Type>>> aliases;
  aliases.emplace_back(name, std::move(type));
  return defineTypedefs(std::move(aliases));
}

bool TypeContext::defineTypedefs(
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> aliases) {
  if (aliases.empty()) { return false; }

  auto updated = typedefs_;

  for (auto &alias : aliases) {
    if (alias.first.empty() || !alias.second || updated.find(alias.first) != updated.end()) {
      return false;
    }

    updated.emplace(alias.first, std::move(alias.second));
  }

  typedefs_.swap(updated);
  return true;
}

}} // namespace edon::ffi
