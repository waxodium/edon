#include "types.hpp"
#include "signature.hpp"

#include <ffi.h>

#include <limits>
#include <new>
#include <unordered_set>
#include <utility>
#include <vector>

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

  case TypeKind::Enum: return &ffi_type_sint32;

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

  case TypeKind::Function:
  case TypeKind::Struct:
  case TypeKind::Array: return nullptr;
  }

  return nullptr;
}

bool isPreparing(const Type *type) { return preparing.find(type) != preparing.end(); }

bool beginPreparing(const Type *type) { return preparing.insert(type).second; }

void endPreparing(const Type *type) { preparing.erase(type); }

bool prepareFunction(Type &type) {
  if (type.complete) return true;

  if (!type.functionSignature) return false;

  if (!prepareSignature(*type.functionSignature)) return false;

  type.complete = true;

  return true;
}

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

  if (aggregate->size == 0 || aggregate->alignment == 0) {
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

  for (std::size_t i = 0; i < elements.size(); ++i) elementStorage[i] = elements[i];

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

bool prepareEnum(Type &type) {
  if (type.complete && type.ffi) return true;

  if (type.enumValues.empty()) return false;

  ffi_type *ffi = &ffi_type_sint32;

  type.ffi = ffi;
  type.size = ffi->size;
  type.alignment = ffi->alignment;
  type.complete = true;

  return true;
}

bool prepareTypeInternal(const std::shared_ptr<Type> &type) {
  if (!type) return false;

  if (type->complete && type->ffi) return true;

  if (type->kind == TypeKind::Function && type->complete) return true;

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
    case TypeKind::Function: result = prepareFunction(*type); break;

    case TypeKind::Struct: result = prepareStruct(*type); break;

    case TypeKind::Array: result = prepareArray(*type); break;

    case TypeKind::Enum: result = prepareEnum(*type); break;

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

std::shared_ptr<Type> makeEnum(const std::string &name, std::vector<EnumValue> values) {
  auto type = std::make_shared<Type>();

  type->kind = TypeKind::Enum;
  type->name = name;
  type->enumValues = std::move(values);

  return type;
}

std::shared_ptr<Type> makeArray(std::shared_ptr<Type> element, std::size_t count) {
  auto type = std::make_shared<Type>();

  type->kind = TypeKind::Array;
  type->element = std::move(element);
  type->count = count;

  return type;
}

bool prepareType(const std::shared_ptr<Type> &type) { return prepareTypeInternal(type); }

bool prepareSignature(Signature &signature) {
  if (!signature.returns) return false;

  if (signature.args.size() > std::numeric_limits<unsigned int>::max()) return false;

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

    ffiArgs.push_back(argument->ffi);
  }

  ffi_cif cif{};

  ffi_status status;

  if (signature.variadic) {
    const unsigned int fixedCount = static_cast<unsigned int>(ffiArgs.size());

    status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, fixedCount, fixedCount, signature.returns->ffi,
                              ffiArgs.empty() ? nullptr : ffiArgs.data());
  } else {
    status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, static_cast<unsigned int>(ffiArgs.size()),
                          signature.returns->ffi, ffiArgs.empty() ? nullptr : ffiArgs.data());
  }

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

  case TypeKind::Function: return "function";

  case TypeKind::Struct: return "struct";

  case TypeKind::Array: return "array";

  case TypeKind::Enum: return "enum";
  }

  return "unknown";
}

bool isPrimitive(TypeKind kind) { return primitiveFFIType(kind) != nullptr; }

bool isInteger(TypeKind kind) {
  return kind == TypeKind::Bool || kind == TypeKind::Char || kind == TypeKind::Int8 ||
         kind == TypeKind::UInt8 || kind == TypeKind::Int16 || kind == TypeKind::UInt16 ||
         kind == TypeKind::Int32 || kind == TypeKind::UInt32 || kind == TypeKind::Int64 ||
         kind == TypeKind::UInt64 || kind == TypeKind::Size || kind == TypeKind::SSize ||
         kind == TypeKind::Enum;
}

bool isSignedInteger(TypeKind kind) {
  return kind == TypeKind::Int8 || kind == TypeKind::Int16 || kind == TypeKind::Int32 ||
         kind == TypeKind::Int64 || kind == TypeKind::SSize || kind == TypeKind::Enum;
}

bool isUnsignedInteger(TypeKind kind) {
  return kind == TypeKind::UInt8 || kind == TypeKind::UInt16 || kind == TypeKind::UInt32 ||
         kind == TypeKind::UInt64 || kind == TypeKind::Size;
}

bool isFloating(TypeKind kind) { return kind == TypeKind::Float || kind == TypeKind::Double; }

bool isPointer(TypeKind kind) { return kind == TypeKind::Pointer; }

bool isAggregate(TypeKind kind) { return kind == TypeKind::Struct || kind == TypeKind::Array; }

}} // namespace edon::ffi
