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

class ContextTransaction {
public:
  explicit ContextTransaction(TypeContext *context)
      : context_(context),
        checkpoint_(context ? context->checkpoint() : TypeContext::Checkpoint{}) {}

  ContextTransaction(const ContextTransaction &) = delete;
  ContextTransaction &operator=(const ContextTransaction &) = delete;

  ~ContextTransaction() {
    if (context_ && !committed_) { context_->rollback(std::move(checkpoint_)); }
  }

  void commit() { committed_ = true; }

private:
  TypeContext *context_;
  TypeContext::Checkpoint checkpoint_;
  bool committed_ = false;
};

std::string trim(const std::string &value) {
  std::size_t first = 0;

  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }

  std::size_t last = value.size();

  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) { --last; }

  return value.substr(first, last - first);
}

} // namespace

bool parseFieldDeclaration(const std::string &source, TypeContext *context, Field &field,
                           std::string &error) {
  field = Field{};

  const std::string input = trim(source);

  if (input.empty()) {
    error = "empty struct field";
    return false;
  }

  std::size_t nameEnd = input.size();

  while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(input[nameEnd - 1]))) { --nameEnd; }

  while (nameEnd > 0 && input[nameEnd - 1] == ']') {
    int depth = 0;
    std::size_t position = nameEnd;

    while (position > 0) {
      --position;

      if (input[position] == ']') {
        ++depth;
      } else if (input[position] == '[') {
        --depth;

        if (depth == 0) {
          nameEnd = position;
          break;
        }
      }
    }

    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(input[nameEnd - 1]))) {
      --nameEnd;
    }
  }

  const std::size_t suffixStart = nameEnd;
  std::size_t nameStart = nameEnd;

  while (nameStart > 0) {
    const char character = input[nameStart - 1];

    if (std::isalnum(static_cast<unsigned char>(character)) || character == '_') {
      --nameStart;
      continue;
    }

    break;
  }

  if (nameStart == nameEnd) {
    error = "struct field requires a name: " + input;
    return false;
  }

  const std::string name = input.substr(nameStart, nameEnd - nameStart);
  std::string typeSource = trim(input.substr(0, nameStart));
  const std::string suffix = input.substr(suffixStart);

  if (!suffix.empty()) {
    if (!typeSource.empty()) typeSource += ' ';
    typeSource += suffix;
  }

  std::shared_ptr<Type> type;

  if (!parseType(typeSource, type, error, context)) {
    error = "unsupported struct field type: " + typeSource;
    return false;
  }

  field.name = name;
  field.type = std::move(type);

  return true;
}

bool splitStructFields(const std::string &source, std::vector<std::string> &fields) {
  fields.clear();

  std::size_t start = 0;
  std::size_t parentheses = 0;
  std::size_t brackets = 0;

  for (std::size_t i = 0; i < source.size(); ++i) {
    const char character = source[i];

    if (character == '(') {
      ++parentheses;
      continue;
    }

    if (character == ')') {
      if (parentheses == 0) return false;
      --parentheses;
      continue;
    }

    if (character == '[') {
      ++brackets;
      continue;
    }

    if (character == ']') {
      if (brackets == 0) return false;
      --brackets;
      continue;
    }

    if (character != ';' || parentheses != 0 || brackets != 0) { continue; }

    const std::string field = trim(source.substr(start, i - start));
    if (field.empty()) return false;

    fields.push_back(field);
    start = i + 1;
  }

  if (parentheses != 0 || brackets != 0) return false;

  return trim(source.substr(start)).empty();
}

bool parseStructDefinition(const std::string &source, TypeContext *context, std::string &error) {
  error.clear();

  const std::string input = trim(source);

  if (input.size() < 9 || input.compare(0, 6, "struct") != 0 ||
      !std::isspace(static_cast<unsigned char>(input[6]))) {
    error = "invalid struct definition";
    return false;
  }

  if (!context) {
    error = "missing type context";
    return false;
  }

  ContextTransaction transaction(context);

  std::size_t position = 6;

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  const std::size_t nameStart = position;

  while (position < input.size() &&
         (std::isalnum(static_cast<unsigned char>(input[position])) || input[position] == '_')) {
    ++position;
  }

  if (position == nameStart) {
    error = "struct definition requires a name";
    return false;
  }

  const std::string name = input.substr(nameStart, position - nameStart);

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  if (position >= input.size() || input[position] != '{') {
    error = "expected '{' after struct name";
    return false;
  }

  const std::size_t bodyStart = position + 1;
  std::size_t depth = 1;
  std::size_t bodyEnd = std::string::npos;

  for (std::size_t i = bodyStart; i < input.size(); ++i) {
    if (input[i] == '{') {
      ++depth;
    } else if (input[i] == '}') {
      --depth;

      if (depth == 0) {
        bodyEnd = i;
        break;
      }
    }
  }

  if (bodyEnd == std::string::npos) {
    error = "unterminated struct definition";
    return false;
  }

  if (!trim(input.substr(bodyEnd + 1)).empty()) {
    error = "unexpected tokens after struct definition";
    return false;
  }

  std::vector<std::string> fieldSources;

  if (!splitStructFields(input.substr(bodyStart, bodyEnd - bodyStart), fieldSources)) {
    error = "invalid struct field list";
    return false;
  }

  if (fieldSources.empty()) {
    error = "struct must contain at least one field";
    return false;
  }

  const bool hadStructTag = static_cast<bool>(context->findStruct(name));
  auto structType = context->getOrCreateStruct(name);

  if (!structType) {
    error = "failed to create struct: " + name;
    return false;
  }

  if (structType->complete) {
    error = "struct already defined: " + name;
    return false;
  }

  std::vector<Field> fields;

  try {
    fields.reserve(fieldSources.size());
  } catch (...) {
    error = "struct field allocation failed";

    if (!hadStructTag) { context->discardIncompleteStruct(name); }

    return false;
  }

  for (const std::string &fieldSource : fieldSources) {
    Field field;

    if (!parseFieldDeclaration(fieldSource, context, field, error)) {
      error = "invalid struct field: " + error;

      if (!hadStructTag) { context->discardIncompleteStruct(name); }

      return false;
    }

    for (const Field &existing : fields) {
      if (existing.name == field.name) {
        error = "duplicate struct field: " + field.name;

        if (!hadStructTag) { context->discardIncompleteStruct(name); }

        return false;
      }
    }

    fields.push_back(std::move(field));
  }

  if (!context->defineStruct(name, std::move(fields))) {
    error = "failed to define struct: " + name;

    if (!hadStructTag) { context->discardIncompleteStruct(name); }

    return false;
  }

  transaction.commit();
  return true;
}

bool parseUnionDefinition(const std::string &source, TypeContext *context, std::string &error) {
  error.clear();

  const std::string input = trim(source);

  if (input.size() < 8 || input.compare(0, 5, "union") != 0) {
    error = "invalid union definition";
    return false;
  }

  if (!context) {
    error = "missing type context";
    return false;
  }

  ContextTransaction transaction(context);

  std::size_t position = 5;

  if (position < input.size() && !std::isspace(static_cast<unsigned char>(input[position]))) {
    error = "invalid union keyword";
    return false;
  }

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  const std::size_t nameStart = position;

  while (position < input.size() &&
         (std::isalnum(static_cast<unsigned char>(input[position])) || input[position] == '_')) {
    ++position;
  }

  if (position == nameStart) {
    error = "union definition requires a name";
    return false;
  }

  const std::string name = input.substr(nameStart, position - nameStart);

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  if (position >= input.size() || input[position] != '{') {
    error = "expected '{' after union name";
    return false;
  }

  const std::size_t bodyStart = position + 1;
  std::size_t depth = 1;
  std::size_t bodyEnd = std::string::npos;

  for (std::size_t i = bodyStart; i < input.size(); ++i) {
    if (input[i] == '{') {
      ++depth;
    } else if (input[i] == '}') {
      --depth;

      if (depth == 0) {
        bodyEnd = i;
        break;
      }
    }
  }

  if (bodyEnd == std::string::npos) {
    error = "unterminated union definition";
    return false;
  }

  if (!trim(input.substr(bodyEnd + 1)).empty()) {
    error = "unexpected tokens after union definition";
    return false;
  }

  std::vector<std::string> fieldSources;

  if (!splitStructFields(input.substr(bodyStart, bodyEnd - bodyStart), fieldSources)) {
    error = "invalid union field list";
    return false;
  }

  if (fieldSources.empty()) {
    error = "union must contain at least one field";
    return false;
  }

  const bool hadUnionTag = static_cast<bool>(context->findUnion(name));
  auto unionType = context->getOrCreateUnion(name);

  if (!unionType) {
    error = "failed to create union: " + name;
    return false;
  }

  if (unionType->complete) {
    error = "union already defined: " + name;
    return false;
  }

  std::vector<Field> fields;

  try {
    fields.reserve(fieldSources.size());
  } catch (...) {
    error = "union field allocation failed";

    if (!hadUnionTag) { context->discardIncompleteUnion(name); }

    return false;
  }

  for (const std::string &fieldSource : fieldSources) {
    Field field;

    if (!parseFieldDeclaration(fieldSource, context, field, error)) {
      error = "invalid union field: " + error;

      if (!hadUnionTag) { context->discardIncompleteUnion(name); }

      return false;
    }

    for (const Field &existing : fields) {
      if (existing.name == field.name) {
        error = "duplicate union field: " + field.name;

        if (!hadUnionTag) { context->discardIncompleteUnion(name); }

        return false;
      }
    }

    fields.push_back(std::move(field));
  }

  if (!context->defineUnion(name, std::move(fields))) {
    error = "failed to define union: " + name;

    if (!hadUnionTag) { context->discardIncompleteUnion(name); }

    return false;
  }

  transaction.commit();
  return true;
}

bool parseEnumDefinition(const std::string &source, TypeContext *context, std::string &error) {
  error.clear();

  const std::string input = trim(source);

  if (!context) {
    error = "missing type context";
    return false;
  }

  ContextTransaction transaction(context);

  if (input.size() < 7 || input.compare(0, 4, "enum") != 0) {
    error = "invalid enum definition";
    return false;
  }

  std::size_t position = 4;

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  const std::size_t nameStart = position;

  while (position < input.size() &&
         (std::isalnum(static_cast<unsigned char>(input[position])) || input[position] == '_')) {
    ++position;
  }

  if (position == nameStart) {
    error = "enum definition requires a name";
    return false;
  }

  const std::string name = input.substr(nameStart, position - nameStart);
  const bool hadEnumTag = static_cast<bool>(context->findEnum(name));
  const auto existingEnum = context->findEnum(name);

  if (existingEnum && existingEnum->complete) {
    error = "enum already defined: " + name;
    return false;
  }

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  if (position >= input.size() || input[position] != '{') {
    error = "expected '{' after enum name";
    return false;
  }

  const std::size_t bodyStart = position + 1;
  std::size_t depth = 1;
  std::size_t bodyEnd = std::string::npos;

  for (std::size_t i = bodyStart; i < input.size(); ++i) {
    if (input[i] == '{') {
      ++depth;
    } else if (input[i] == '}') {
      --depth;

      if (depth == 0) {
        bodyEnd = i;
        break;
      }
    }
  }

  if (bodyEnd == std::string::npos) {
    error = "unterminated enum definition";
    return false;
  }

  if (!trim(input.substr(bodyEnd + 1)).empty()) {
    error = "unexpected tokens after enum definition";
    return false;
  }

  const std::string body = trim(input.substr(bodyStart, bodyEnd - bodyStart));

  if (body.empty()) {
    error = "enum must contain at least one value";
    return false;
  }

  std::vector<EnumValue> values;
  std::size_t start = 0;
  std::int64_t nextValue = 0;

  while (start <= body.size()) {
    const std::size_t comma = body.find(',', start);

    const std::string item =
        trim(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));

    if (item.empty()) {
      error = "empty enum value";
      return false;
    }

    const std::size_t equal = item.find('=');
    std::string valueName;
    std::string valueExpression;

    if (equal == std::string::npos) {
      valueName = trim(item);
    } else {
      valueName = trim(item.substr(0, equal));
      valueExpression = trim(item.substr(equal + 1));

      if (valueExpression.empty()) {
        error = "enum value requires an initializer: " + valueName;
        return false;
      }
    }

    if (valueName.empty()) {
      error = "enum value requires a name";
      return false;
    }

    if (!std::isalpha(static_cast<unsigned char>(valueName[0])) && valueName[0] != '_') {
      error = "invalid enum value name: " + valueName;
      return false;
    }

    for (char character : valueName) {
      if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') {
        error = "invalid enum value name: " + valueName;
        return false;
      }
    }

    for (const EnumValue &existing : values) {
      if (existing.name == valueName) {
        error = "duplicate enum value: " + valueName;
        return false;
      }
    }

    std::int64_t value = nextValue;

    if (!valueExpression.empty()) {
      std::size_t parsed = 0;

      try {
        value = std::stoll(valueExpression, &parsed, 0);
      } catch (...) {
        error = "invalid enum value: " + valueExpression;
        return false;
      }

      if (parsed != valueExpression.size()) {
        error = "invalid enum value: " + valueExpression;
        return false;
      }
    }

    if (value > std::numeric_limits<std::int32_t>::max() ||
        value < std::numeric_limits<std::int32_t>::min()) {
      error = "enum value outside int32 range: " + valueName;
      return false;
    }

    values.push_back(EnumValue{valueName, value});

    if (value == std::numeric_limits<std::int64_t>::max()) {
      if (comma != std::string::npos) {
        error = "enum value overflow";
        return false;
      }
    } else {
      nextValue = value + 1;
    }

    if (comma == std::string::npos) { break; }

    start = comma + 1;
  }

  if (!context->defineEnum(name, std::move(values))) {
    error = "failed to define enum: " + name;

    if (!hadEnumTag) { context->discardIncompleteEnum(name); }

    return false;
  }

  transaction.commit();
  return true;
}

}} // namespace edon::ffi
