#include "types.hpp"

#include <ffi.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>

namespace edon { namespace ffi {

namespace {

thread_local std::unordered_set<const Type *> preparing;

ffi_type *primitiveFFIType(TypeKind kind) {
  switch (kind) {
  case TypeKind::Void: return &ffi_type_void;

  case TypeKind::Bool: return &ffi_type_uint8;

  case TypeKind::Char: return &ffi_type_schar;

  case TypeKind::Int8: return &ffi_type_sint8;

  case TypeKind::UInt8: return &ffi_type_uint8;

  case TypeKind::Int16: return &ffi_type_sint16;

  case TypeKind::UInt16: return &ffi_type_uint16;

  case TypeKind::Int32: return &ffi_type_sint32;

  case TypeKind::UInt32: return &ffi_type_uint32;

  case TypeKind::Int64: return &ffi_type_sint64;

  case TypeKind::UInt64: return &ffi_type_uint64;

  case TypeKind::Size:
    if constexpr (sizeof(std::size_t) == 4) return &ffi_type_uint32;
    else
      return &ffi_type_uint64;

  case TypeKind::SSize:
    if constexpr (sizeof(std::ptrdiff_t) == 4) return &ffi_type_sint32;
    else
      return &ffi_type_sint64;

  case TypeKind::Float: return &ffi_type_float;

  case TypeKind::Double: return &ffi_type_double;

  case TypeKind::Pointer: return &ffi_type_pointer;

  case TypeKind::Struct:
  case TypeKind::Array: return nullptr;
  }

  return nullptr;
}

bool isPreparing(const Type *type) { return preparing.find(type) != preparing.end(); }

bool beginPreparing(const Type *type) { return preparing.insert(type).second; }

void endPreparing(const Type *type) { preparing.erase(type); }

bool prepareStruct(Type &type) {
  if (type.fields.empty()) return false;

  std::vector<ffi_type *> elements;
  std::vector<std::size_t> offsets;

  try {
    elements.reserve(type.fields.size() + 1);
    offsets.resize(type.fields.size());
  } catch (...) { return false; }

  for (const Field &field : type.fields) {
    if (!field.type) return false;

    if (!prepareType(field.type)) return false;

    if (!field.type->ffi) return false;

    elements.push_back(field.type->ffi);
  }

  elements.push_back(nullptr);

  auto *aggregate = new (std::nothrow) ffi_type{};

  if (!aggregate) return false;

  auto **elementStorage = new (std::nothrow) ffi_type *[elements.size()];

  if (!elementStorage) {
    delete aggregate;
    return false;
  }

  std::copy(elements.begin(), elements.end(), elementStorage);

  aggregate->size = 0;
  aggregate->alignment = 0;
  aggregate->type = FFI_TYPE_STRUCT;
  aggregate->elements = elementStorage;

  const ffi_status status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, aggregate, offsets.data());

  if (status != FFI_OK) {
    delete[] elementStorage;
    delete aggregate;
    return false;
  }

  type.ffi = aggregate;
  type.size = aggregate->size;
  type.alignment = aggregate->alignment;

  for (std::size_t i = 0; i < type.fields.size(); ++i) type.fields[i].offset = offsets[i];

  type.complete = true;

  return true;
}

bool prepareArray(Type &type) {
  if (!type.element || type.count == 0) return false;

  if (!prepareType(type.element)) return false;

  if (!type.element->ffi) return false;

  if (type.count == std::numeric_limits<std::size_t>::max()) return false;

  const std::size_t elementCount = type.count + 1;

  std::vector<ffi_type *> elements;

  try {
    elements.resize(elementCount);
  } catch (...) { return false; }

  for (std::size_t i = 0; i < type.count; ++i) elements[i] = type.element->ffi;

  elements[type.count] = nullptr;

  auto *aggregate = new (std::nothrow) ffi_type{};

  if (!aggregate) return false;

  auto **elementStorage = new (std::nothrow) ffi_type *[elements.size()];

  if (!elementStorage) {
    delete aggregate;
    return false;
  }

  std::copy(elements.begin(), elements.end(), elementStorage);

  aggregate->size = 0;
  aggregate->alignment = 0;
  aggregate->type = FFI_TYPE_STRUCT;
  aggregate->elements = elementStorage;

  const ffi_status status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, aggregate, nullptr);

  if (status != FFI_OK) {
    delete[] elementStorage;
    delete aggregate;
    return false;
  }

  type.ffi = aggregate;
  type.size = aggregate->size;
  type.alignment = aggregate->alignment;
  type.complete = true;

  return true;
}

bool prepareTypeInternal(const std::shared_ptr<Type> &type) {
  if (!type) return false;

  if (type->complete && type->ffi) return true;

  if (isPreparing(type.get())) return false;

  if (!beginPreparing(type.get())) return false;

  bool result = false;

  if (isPrimitive(type->kind)) {
    ffi_type *ffi = primitiveFFIType(type->kind);

    if (ffi) {
      type->ffi = ffi;
      type->size = ffi->size;
      type->alignment = ffi->alignment;
      type->complete = true;
      result = true;
    }
  } else {
    switch (type->kind) {
    case TypeKind::Struct: result = prepareStruct(*type); break;

    case TypeKind::Array: result = prepareArray(*type); break;

    case TypeKind::Void:
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::Int8:
    case TypeKind::UInt8:
    case TypeKind::Int16:
    case TypeKind::UInt16:
    case TypeKind::Int32:
    case TypeKind::UInt32:
    case TypeKind::Int64:
    case TypeKind::UInt64:
    case TypeKind::Size:
    case TypeKind::SSize:
    case TypeKind::Float:
    case TypeKind::Double:
    case TypeKind::Pointer: break;
    }
  }

  endPreparing(type.get());

  return result;
}

std::string removeWhitespace(const std::string &input) {
  std::string result;
  result.reserve(input.size());

  for (char character : input) {
    if (!std::isspace(static_cast<unsigned char>(character))) result += character;
  }

  return result;
}

std::shared_ptr<Type> parseBaseType(const std::string &name) {
  if (name == "void") return makeType(TypeKind::Void);

  if (name == "bool" || name == "_Bool") return makeType(TypeKind::Bool);

  if (name == "char") return makeType(TypeKind::Char);

  if (name == "signedchar") return makeType(TypeKind::Int8);

  if (name == "unsignedchar") return makeType(TypeKind::UInt8);

  if (name == "int8" || name == "int8_t") return makeType(TypeKind::Int8);

  if (name == "uint8" || name == "uint8_t") return makeType(TypeKind::UInt8);

  if (name == "short" || name == "shortint" || name == "signedshort" || name == "signedshortint") {
    return makeType(TypeKind::Int16);
  }

  if (name == "unsignedshort" || name == "unsignedshortint") { return makeType(TypeKind::UInt16); }

  if (name == "int" || name == "signed" || name == "signedint") {
    return makeType(TypeKind::Int32);
  }

  if (name == "unsigned" || name == "unsignedint") { return makeType(TypeKind::UInt32); }

  if (name == "int16" || name == "int16_t") return makeType(TypeKind::Int16);

  if (name == "uint16" || name == "uint16_t") return makeType(TypeKind::UInt16);

  if (name == "int32" || name == "int32_t") return makeType(TypeKind::Int32);

  if (name == "uint32" || name == "uint32_t") return makeType(TypeKind::UInt32);

  if (name == "longlong" || name == "longlongint" || name == "signedlonglong" ||
      name == "signedlonglongint") {
    return makeType(TypeKind::Int64);
  }

  if (name == "unsignedlonglong" || name == "unsignedlonglongint") {
    return makeType(TypeKind::UInt64);
  }

  if (name == "long") return makeType(TypeKind::Int64);

  if (name == "unsignedlong") return makeType(TypeKind::UInt64);

  if (name == "int64" || name == "int64_t") return makeType(TypeKind::Int64);

  if (name == "uint64" || name == "uint64_t") return makeType(TypeKind::UInt64);

  if (name == "size" || name == "size_t") return makeType(TypeKind::Size);

  if (name == "ssize" || name == "ssize_t") return makeType(TypeKind::SSize);

  if (name == "float") return makeType(TypeKind::Float);

  if (name == "double") return makeType(TypeKind::Double);

  return nullptr;
}

std::shared_ptr<Type> parseStructType(const std::string &name) {
  constexpr const char *prefix = "struct";

  if (name.rfind(prefix, 0) != 0) return nullptr;

  const std::string structName = name.substr(6);

  if (structName.empty()) return nullptr;

  return makeStruct(structName, {});
}

struct Qualifiers {
  bool isConst = false;
  bool isVolatile = false;
  bool isRestrict = false;
};

bool consumeQualifier(const std::string &source, std::size_t &position, const char *qualifier) {
  const std::size_t length = std::char_traits<char>::length(qualifier);

  if (source.compare(position, length, qualifier) != 0) return false;

  position += length;
  return true;
}

Qualifiers parseQualifiers(const std::string &source, std::size_t &position) {
  Qualifiers qualifiers;

  bool consumed = true;

  while (consumed) {
    consumed = false;

    if (consumeQualifier(source, position, "const")) {
      qualifiers.isConst = true;
      consumed = true;
      continue;
    }

    if (consumeQualifier(source, position, "volatile")) {
      qualifiers.isVolatile = true;
      consumed = true;
      continue;
    }

    if (consumeQualifier(source, position, "restrict")) {
      qualifiers.isRestrict = true;
      consumed = true;
    }
  }

  return qualifiers;
}

struct DeclaratorOp {
  enum class Kind { Pointer, Array };

  Kind kind = Kind::Pointer;
  std::size_t count = 0;

  bool isConst = false;
  bool isVolatile = false;
  bool isRestrict = false;
};

class DeclaratorParser {
public:
  explicit DeclaratorParser(const std::string &source) : source_(source) {}

  bool parse(std::vector<DeclaratorOp> &operations) {
    position_ = 0;

    if (!parseDeclarator(operations)) return false;

    return position_ == source_.size();
  }

private:
  bool parseDeclarator(std::vector<DeclaratorOp> &operations) {
    std::vector<DeclaratorOp> pointers;

    while (consume('*')) {
      DeclaratorOp operation;
      operation.kind = DeclaratorOp::Kind::Pointer;

      const Qualifiers qualifiers = parseQualifiers(source_, position_);

      operation.isConst = qualifiers.isConst;
      operation.isVolatile = qualifiers.isVolatile;
      operation.isRestrict = qualifiers.isRestrict;

      pointers.push_back(operation);
    }

    if (position_ < source_.size() && source_[position_] == '(') {
      ++position_;

      std::vector<DeclaratorOp> nested;

      if (!parseDeclarator(nested)) return false;

      if (!consume(')')) return false;

      std::vector<DeclaratorOp> suffixes;

      if (!parseArrays(suffixes)) return false;

      operations.insert(operations.end(), suffixes.begin(), suffixes.end());

      operations.insert(operations.end(), nested.begin(), nested.end());

      operations.insert(operations.end(), pointers.begin(), pointers.end());

      return true;
    }

    if (!parseArrays(operations)) return false;

    operations.insert(operations.begin(), pointers.begin(), pointers.end());

    return true;
  }

  bool parseArrays(std::vector<DeclaratorOp> &operations) {
    while (consume('[')) {
      std::size_t count = 0;

      if (position_ >= source_.size()) return false;

      if (source_[position_] == ']') return false;

      while (position_ < source_.size() && source_[position_] != ']') {
        const char character = source_[position_];

        if (!std::isdigit(static_cast<unsigned char>(character))) { return false; }

        const std::size_t digit = static_cast<std::size_t>(character - '0');

        if (count > (std::numeric_limits<std::size_t>::max() - digit) / 10) { return false; }

        count = count * 10 + digit;
        ++position_;
      }

      if (!consume(']')) return false;

      DeclaratorOp operation;
      operation.kind = DeclaratorOp::Kind::Array;
      operation.count = count;

      operations.push_back(operation);
    }

    return true;
  }

  bool consume(char character) {
    if (position_ >= source_.size() || source_[position_] != character) { return false; }

    ++position_;
    return true;
  }

  const std::string &source_;
  std::size_t position_ = 0;
};

} // namespace

Type::~Type() {
  if (!ffi || isPrimitive(kind)) return;

  delete[] ffi->elements;
  delete ffi;

  ffi = nullptr;
}

std::shared_ptr<Type> makeType(TypeKind kind) {
  auto type = std::make_shared<Type>();

  type->kind = kind;

  ffi_type *ffi = primitiveFFIType(kind);

  if (!ffi) return type;

  type->ffi = ffi;
  type->size = ffi->size;
  type->alignment = ffi->alignment;
  type->complete = true;

  return type;
}

std::shared_ptr<Type> makeStruct(const std::string &name, std::vector<Field> fields) {
  auto type = std::make_shared<Type>();

  type->kind = TypeKind::Struct;
  type->name = name;
  type->fields = std::move(fields);

  return type;
}

std::shared_ptr<Type> makeArray(std::shared_ptr<Type> element, std::size_t count) {
  auto type = std::make_shared<Type>();

  type->kind = TypeKind::Array;
  type->element = std::move(element);
  type->count = count;

  return type;
}

std::shared_ptr<Type> parseTypeName(const std::string &input) {
  std::string source = removeWhitespace(input);

  if (source.empty()) return nullptr;

  std::size_t qualifierPosition = 0;

  const Qualifiers baseQualifiers = parseQualifiers(source, qualifierPosition);

  source.erase(0, qualifierPosition);

  if (source.empty()) return nullptr;

  const std::size_t declaratorStart = source.find_first_of("*([");

  const std::string baseName =
      declaratorStart == std::string::npos ? source : source.substr(0, declaratorStart);

  const std::string declarator =
      declaratorStart == std::string::npos ? std::string() : source.substr(declaratorStart);

  std::shared_ptr<Type> type = parseBaseType(baseName);

  if (!type) type = parseStructType(baseName);

  if (!type) return nullptr;

  type->isConst = baseQualifiers.isConst;
  type->isVolatile = baseQualifiers.isVolatile;
  type->isRestrict = baseQualifiers.isRestrict;

  if (declarator.empty()) return type;

  std::vector<DeclaratorOp> operations;
  DeclaratorParser parser(declarator);

  if (!parser.parse(operations)) return nullptr;

  for (const DeclaratorOp &operation : operations) {
    if (operation.kind == DeclaratorOp::Kind::Pointer) {
      auto pointer = makeType(TypeKind::Pointer);

      pointer->element = type;
      pointer->isConst = operation.isConst;
      pointer->isVolatile = operation.isVolatile;
      pointer->isRestrict = operation.isRestrict;

      type = std::move(pointer);
      continue;
    }

    type = makeArray(type, operation.count);
  }

  return type;
}

bool prepareType(const std::shared_ptr<Type> &type) { return prepareTypeInternal(type); }

bool prepareSignature(Signature &signature) {
  if (!signature.returns) return false;

  if (signature.args.size() > std::numeric_limits<unsigned int>::max()) { return false; }

  if (!prepareType(signature.returns)) return false;

  if (!signature.returns->ffi) return false;

  std::vector<ffi_type *> ffiArgs;

  try {
    ffiArgs.reserve(signature.args.size());
  } catch (...) { return false; }

  for (const auto &argument : signature.args) {
    if (!argument) return false;

    if (!prepareType(argument)) return false;

    if (!argument->ffi) return false;

    try {
      ffiArgs.push_back(argument->ffi);
    } catch (...) { return false; }
  }

  ffi_cif cif{};

  const ffi_status status =
      ffi_prep_cif(&cif, FFI_DEFAULT_ABI, static_cast<unsigned int>(ffiArgs.size()),
                   signature.returns->ffi, ffiArgs.empty() ? nullptr : ffiArgs.data());

  if (status != FFI_OK) return false;

  signature.cif = cif;
  signature.ffiArgs = std::move(ffiArgs);
  signature.prepared = true;

  return true;
}

const char *typeName(TypeKind kind) {
  switch (kind) {
  case TypeKind::Void: return "void";

  case TypeKind::Bool: return "bool";

  case TypeKind::Char: return "char";

  case TypeKind::Int8: return "int8";

  case TypeKind::UInt8: return "uint8";

  case TypeKind::Int16: return "int16";

  case TypeKind::UInt16: return "uint16";

  case TypeKind::Int32: return "int32";

  case TypeKind::UInt32: return "uint32";

  case TypeKind::Int64: return "int64";

  case TypeKind::UInt64: return "uint64";

  case TypeKind::Size: return "size";

  case TypeKind::SSize: return "ssize";

  case TypeKind::Float: return "float";

  case TypeKind::Double: return "double";

  case TypeKind::Pointer: return "pointer";

  case TypeKind::Struct: return "struct";

  case TypeKind::Array: return "array";
  }

  return "unknown";
}

bool isPrimitive(TypeKind kind) { return primitiveFFIType(kind) != nullptr; }

bool isInteger(TypeKind kind) {
  return kind == TypeKind::Bool || kind == TypeKind::Char || kind == TypeKind::Int8 ||
         kind == TypeKind::UInt8 || kind == TypeKind::Int16 || kind == TypeKind::UInt16 ||
         kind == TypeKind::Int32 || kind == TypeKind::UInt32 || kind == TypeKind::Int64 ||
         kind == TypeKind::UInt64 || kind == TypeKind::Size || kind == TypeKind::SSize;
}

bool isSignedInteger(TypeKind kind) {
  return kind == TypeKind::Int8 || kind == TypeKind::Int16 || kind == TypeKind::Int32 ||
         kind == TypeKind::Int64 || kind == TypeKind::SSize;
}

bool isUnsignedInteger(TypeKind kind) {
  return kind == TypeKind::UInt8 || kind == TypeKind::UInt16 || kind == TypeKind::UInt32 ||
         kind == TypeKind::UInt64 || kind == TypeKind::Size;
}

bool isFloating(TypeKind kind) { return kind == TypeKind::Float || kind == TypeKind::Double; }

bool isPointer(TypeKind kind) { return kind == TypeKind::Pointer; }

bool isAggregate(TypeKind kind) { return kind == TypeKind::Struct || kind == TypeKind::Array; }

}} // namespace edon::ffi
