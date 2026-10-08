#pragma once

#include <ffi.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edon { namespace ffi {

enum class TypeKind {
  Void,
  Bool,
  Char,
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Size,
  SSize,
  Float,
  Double,
  Pointer,
  Struct,
  Array,
  Function,
  Enum
};

struct Type;
struct Signature;

struct Field {
  std::string name;
  std::shared_ptr<Type> type;
  std::size_t offset = 0;
};

struct EnumValue {
  std::string name;
  std::int64_t value = 0;
};

struct Type {
  TypeKind kind = TypeKind::Void;
  std::string name;

  std::size_t size = 0;
  std::size_t alignment = 0;

  bool complete = false;

  bool isConst = false;
  bool isVolatile = false;
  bool isRestrict = false;

  ffi_type *ffi = nullptr;

  std::vector<Field> fields;
  std::vector<EnumValue> enumValues;

  std::shared_ptr<Type> element;
  std::size_t count = 0;

  std::shared_ptr<Signature> functionSignature;

  Type() = default;
  ~Type();

  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;

  Type(Type &&) = delete;
  Type &operator=(Type &&) = delete;
};

std::shared_ptr<Type> makeType(TypeKind kind);

std::shared_ptr<Type> makeStruct(const std::string &name, std::vector<Field> fields);

std::shared_ptr<Type> makeEnum(const std::string &name, std::vector<EnumValue> values);

std::shared_ptr<Type> makeArray(std::shared_ptr<Type> element, std::size_t count);

bool prepareType(const std::shared_ptr<Type> &type);

const char *typeName(TypeKind kind);

bool isPrimitive(TypeKind kind);
bool isInteger(TypeKind kind);
bool isSignedInteger(TypeKind kind);
bool isUnsignedInteger(TypeKind kind);
bool isFloating(TypeKind kind);
bool isPointer(TypeKind kind);
bool isAggregate(TypeKind kind);

}} // namespace edon::ffi
