#include "parser.hpp"
#include "signature.hpp"

#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace edon { namespace ffi {

std::shared_ptr<Type> TypeContext::findStruct(const std::string &name) const {
  const auto it = structs_.find(name);

  if (it == structs_.end()) return nullptr;

  return it->second;
}

std::shared_ptr<Type> TypeContext::getOrCreateStruct(const std::string &name) {
  const auto it = structs_.find(name);

  if (it != structs_.end()) return it->second;

  auto type = makeStruct(name, {});

  if (!type) return nullptr;

  structs_.emplace(name, type);

  return type;
}

bool TypeContext::defineStruct(const std::string &name, std::vector<Field> fields) {
  auto type = getOrCreateStruct(name);

  if (!type || type->complete) return false;

  type->fields = std::move(fields);

  return prepareType(type);
}

namespace {

struct Qualifiers {
  bool isConst = false;
  bool isVolatile = false;
  bool isRestrict = false;
};

struct DeclaratorOp {
  enum class Kind { Pointer, Array, Function };

  Kind kind = Kind::Pointer;

  std::size_t count = 0;

  bool isConst = false;
  bool isVolatile = false;
  bool isRestrict = false;

  std::vector<std::shared_ptr<Type>> arguments;
  bool variadic = false;
};

struct Declarator {
  std::vector<DeclaratorOp> pointers;
  std::vector<DeclaratorOp> suffixes;
  std::shared_ptr<Declarator> nested;

  std::string name;
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

std::string removePreprocessorDirectives(const std::string &source) {
  std::string result;
  result.reserve(source.size());

  bool atLineStart = true;
  bool inString = false;
  bool inChar = false;
  bool escaped = false;
  bool inLineComment = false;
  bool inBlockComment = false;
  bool directive = false;
  bool directiveContinuation = false;

  for (std::size_t i = 0; i < source.size(); ++i) {
    const char character = source[i];

    if (inLineComment) {
      result.push_back(character);

      if (character == '\n') {
        inLineComment = false;
        atLineStart = true;

        if (directive) {
          directive = false;
          directiveContinuation = false;
        }
      }

      continue;
    }

    if (inBlockComment) {
      result.push_back(character);

      if (character == '*' && i + 1 < source.size() && source[i + 1] == '/') {
        result.push_back('/');
        ++i;
        inBlockComment = false;
      }

      if (character == '\n') { atLineStart = true; }

      continue;
    }

    if (inString) {
      result.push_back(character);

      if (escaped) {
        escaped = false;
        continue;
      }

      if (character == '\\') {
        escaped = true;
        continue;
      }

      if (character == '"') { inString = false; }

      continue;
    }

    if (inChar) {
      result.push_back(character);

      if (escaped) {
        escaped = false;
        continue;
      }

      if (character == '\\') {
        escaped = true;
        continue;
      }

      if (character == '\'') { inChar = false; }

      continue;
    }

    if (character == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      result.push_back('/');
      result.push_back('/');
      ++i;
      inLineComment = true;
      continue;
    }

    if (character == '/' && i + 1 < source.size() && source[i + 1] == '*') {
      result.push_back('/');
      result.push_back('*');
      ++i;
      inBlockComment = true;
      continue;
    }

    if (character == '"') {
      result.push_back(character);
      inString = true;
      atLineStart = false;
      continue;
    }

    if (character == '\'') {
      result.push_back(character);
      inChar = true;
      atLineStart = false;
      continue;
    }

    if (atLineStart) {
      if (character == ' ' || character == '\t' || character == '\r' || character == '\f' ||
          character == '\v') {
        result.push_back(character);
        continue;
      }

      if (character == '#') {
        directive = true;
        directiveContinuation = false;

        result.push_back('\n');
        continue;
      }

      atLineStart = false;
    }

    if (directive) {
      if (character == '\\') {
        directiveContinuation = true;
        continue;
      }

      if (character == '\n') {
        result.push_back('\n');

        if (directiveContinuation) {
          directiveContinuation = false;
          continue;
        }

        directive = false;
        atLineStart = true;
        continue;
      }

      continue;
    }

    result.push_back(character);

    if (character == '\n') { atLineStart = true; }
  }

  return result;
}

std::string removeWhitespace(const std::string &value) {
  std::string result;

  result.reserve(value.size());

  for (char character : value) {
    if (!std::isspace(static_cast<unsigned char>(character))) { result.push_back(character); }
  }

  return result;
}

bool consumeQualifier(const std::string &source, std::size_t &position, const char *qualifier) {
  const std::size_t length = std::char_traits<char>::length(qualifier);

  if (source.compare(position, length, qualifier) != 0) { return false; }

  position += length;

  return true;
}

Qualifiers parseQualifiers(const std::string &source, std::size_t &position) {
  Qualifiers qualifiers;

  while (position < source.size()) {
    if (consumeQualifier(source, position, "const")) {
      qualifiers.isConst = true;
      continue;
    }

    if (consumeQualifier(source, position, "volatile")) {
      qualifiers.isVolatile = true;
      continue;
    }

    if (consumeQualifier(source, position, "restrict")) {
      qualifiers.isRestrict = true;
      continue;
    }

    break;
  }

  return qualifiers;
}

std::shared_ptr<Type> parseBaseType(const std::string &name) {
  if (name == "void") return makeType(TypeKind::Void);

  if (name == "bool" || name == "_Bool") { return makeType(TypeKind::Bool); }

  if (name == "char") return makeType(TypeKind::Char);

  if (name == "signedchar") { return makeType(TypeKind::Char); }

  if (name == "unsignedchar") { return makeType(TypeKind::UInt8); }

  if (name == "int8" || name == "int8_t") { return makeType(TypeKind::Int8); }

  if (name == "uint8" || name == "uint8_t") { return makeType(TypeKind::UInt8); }

  if (name == "short" || name == "shortint" || name == "signedshort" || name == "signedshortint") {
    return makeType(TypeKind::Int16);
  }

  if (name == "unsignedshort" || name == "unsignedshortint") { return makeType(TypeKind::UInt16); }

  if (name == "int" || name == "signed" || name == "signedint") {
    return makeType(TypeKind::Int32);
  }

  if (name == "unsigned" || name == "unsignedint") { return makeType(TypeKind::UInt32); }

  if (name == "int16" || name == "int16_t") { return makeType(TypeKind::Int16); }

  if (name == "uint16" || name == "uint16_t") { return makeType(TypeKind::UInt16); }

  if (name == "int32" || name == "int32_t") { return makeType(TypeKind::Int32); }

  if (name == "uint32" || name == "uint32_t") { return makeType(TypeKind::UInt32); }

  if (name == "longlong" || name == "longlongint" || name == "signedlonglong" ||
      name == "signedlonglongint") {
    return makeType(TypeKind::Int64);
  }

  if (name == "unsignedlonglong" || name == "unsignedlonglongint") {
    return makeType(TypeKind::UInt64);
  }

  if (name == "long") { return makeType(TypeKind::Int64); }

  if (name == "unsignedlong") { return makeType(TypeKind::UInt64); }

  if (name == "int64" || name == "int64_t") { return makeType(TypeKind::Int64); }

  if (name == "uint64" || name == "uint64_t") { return makeType(TypeKind::UInt64); }

  if (name == "size" || name == "size_t") { return makeType(TypeKind::Size); }

  if (name == "ssize" || name == "ssize_t") { return makeType(TypeKind::SSize); }

  if (name == "float") { return makeType(TypeKind::Float); }

  if (name == "double") { return makeType(TypeKind::Double); }

  return nullptr;
}

std::shared_ptr<Type> parseStructType(const std::string &name, TypeContext *context) {
  if (name.size() <= 6) return nullptr;

  if (name.compare(0, 6, "struct") != 0) { return nullptr; }

  const std::string structName = name.substr(6);

  if (structName.empty() || !context) { return nullptr; }

  return context->getOrCreateStruct(structName);
}

bool splitArguments(const std::string &source, std::vector<std::string> &arguments) {
  arguments.clear();

  if (source.empty()) return true;

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

    if (character != ',') continue;

    if (parentheses != 0 || brackets != 0) continue;

    const std::string argument = trim(source.substr(start, i - start));

    if (argument.empty()) return false;

    arguments.push_back(argument);
    start = i + 1;
  }

  if (parentheses != 0 || brackets != 0) { return false; }

  const std::string argument = trim(source.substr(start));

  if (argument.empty()) return false;

  arguments.push_back(argument);

  return true;
}

bool splitFunction(const std::string &source, std::string &returnType, std::string &name,
                   std::string &arguments) {
  const std::size_t open = source.find('(');

  if (open == std::string::npos) return false;

  std::size_t depth = 0;
  std::size_t close = std::string::npos;

  for (std::size_t i = open; i < source.size(); ++i) {
    const char character = source[i];

    if (character == '(') {
      ++depth;
      continue;
    }

    if (character == ')') {
      if (depth == 0) return false;

      --depth;

      if (depth == 0) {
        close = i;
        break;
      }
    }
  }

  if (close == std::string::npos) return false;

  const std::string trailing = trim(source.substr(close + 1));

  if (!trailing.empty() && trailing != "{}" &&
      !(trailing.size() >= 2 && trailing.front() == '{' && trailing.back() == '}')) {
    return false;
  }

  const std::string prefix = trim(source.substr(0, open));

  arguments = trim(source.substr(open + 1, close - open - 1));

  const std::size_t separator = prefix.find_last_of(" \t\r\n");

  if (separator == std::string::npos) return false;

  returnType = trim(prefix.substr(0, separator));

  name = trim(prefix.substr(separator + 1));

  return !returnType.empty() && !name.empty();
}

std::shared_ptr<Type> parseTypeName(const std::string &input, TypeContext *context);

std::shared_ptr<Type> parseParameterType(const std::string &input, TypeContext *context);

class DeclaratorParser {
public:
  DeclaratorParser(const std::string &source, TypeContext *context)
      : source_(source),
        context_(context) {}

  bool parse(Declarator &declarator) {
    position_ = 0;

    if (!parseDeclarator(declarator)) { return false; }

    return position_ == source_.size();
  }

private:
  bool parseIdentifier(std::string &name) {
    if (position_ >= source_.size()) return false;

    const unsigned char first = static_cast<unsigned char>(source_[position_]);

    if (!std::isalpha(first) && source_[position_] != '_') { return false; }

    const std::size_t start = position_;

    ++position_;

    while (position_ < source_.size()) {
      const unsigned char character = static_cast<unsigned char>(source_[position_]);

      if (!std::isalnum(character) && source_[position_] != '_') { break; }

      ++position_;
    }

    name = source_.substr(start, position_ - start);

    return true;
  }

  bool parseDeclarator(Declarator &declarator) {
    while (consume('*')) {
      DeclaratorOp pointer;

      pointer.kind = DeclaratorOp::Kind::Pointer;

      const Qualifiers qualifiers = parseQualifiers(source_, position_);

      pointer.isConst = qualifiers.isConst;
      pointer.isVolatile = qualifiers.isVolatile;
      pointer.isRestrict = qualifiers.isRestrict;

      declarator.pointers.push_back(std::move(pointer));
    }

    if (position_ < source_.size() && source_[position_] == '(') {
      ++position_;

      auto nested = std::make_shared<Declarator>();

      if (!parseDeclarator(*nested)) { return false; }

      if (!consume(')')) return false;

      declarator.nested = std::move(nested);
    } else if (position_ < source_.size() &&
               (std::isalpha(static_cast<unsigned char>(source_[position_])) ||
                source_[position_] == '_')) {
      if (!parseIdentifier(declarator.name)) { return false; }
    }

    return parseSuffixes(declarator.suffixes);
  }

  bool parseSuffixes(std::vector<DeclaratorOp> &suffixes) {
    while (position_ < source_.size()) {
      if (source_[position_] == '[') {
        ++position_;

        if (position_ >= source_.size()) { return false; }

        if (source_[position_] == ']') { return false; }

        std::size_t count = 0;

        while (position_ < source_.size() && source_[position_] != ']') {
          const char character = source_[position_];

          if (!std::isdigit(static_cast<unsigned char>(character))) { return false; }

          const std::size_t digit = static_cast<std::size_t>(character - '0');

          if (count > (std::numeric_limits<std::size_t>::max() - digit) / 10) { return false; }

          count = count * 10 + digit;

          ++position_;
        }

        if (!consume(']')) return false;

        DeclaratorOp array;

        array.kind = DeclaratorOp::Kind::Array;

        array.count = count;

        suffixes.push_back(std::move(array));

        continue;
      }

      if (source_[position_] == '(') {
        ++position_;

        const std::size_t start = position_;

        std::size_t depth = 1;

        while (position_ < source_.size() && depth != 0) {
          const char character = source_[position_];

          if (character == '(') {
            ++depth;
          } else if (character == ')') {
            --depth;
          }

          if (depth != 0) { ++position_; }
        }

        if (depth != 0) return false;

        const std::string argumentSource = source_.substr(start, position_ - start);

        ++position_;

        DeclaratorOp function;

        function.kind = DeclaratorOp::Kind::Function;

        std::vector<std::string> arguments;

        if (!splitArguments(argumentSource, arguments)) { return false; }

        if (arguments.size() == 1 && arguments[0] == "void") {
          suffixes.push_back(std::move(function));
          continue;
        }

        for (std::size_t i = 0; i < arguments.size(); ++i) {
          const std::string argumentSourceItem = trim(arguments[i]);

          if (argumentSourceItem == "...") {
            if (i + 1 != arguments.size()) { return false; }

            function.variadic = true;
            continue;
          }

          std::shared_ptr<Type> argument = parseParameterType(argumentSourceItem, context_);

          if (!argument) return false;

          function.arguments.push_back(std::move(argument));
        }

        suffixes.push_back(std::move(function));

        continue;
      }

      break;
    }

    return true;
  }

  bool consume(char character) {
    if (position_ >= source_.size() || source_[position_] != character) { return false; }

    ++position_;

    return true;
  }

  const std::string &source_;
  TypeContext *context_ = nullptr;
  std::size_t position_ = 0;
};

std::shared_ptr<Type> makePointer(std::shared_ptr<Type> element, const DeclaratorOp &operation) {
  auto pointer = makeType(TypeKind::Pointer);

  pointer->element = std::move(element);

  pointer->isConst = operation.isConst;
  pointer->isVolatile = operation.isVolatile;
  pointer->isRestrict = operation.isRestrict;

  return pointer;
}

std::shared_ptr<Type> applySuffixes(std::shared_ptr<Type> type,
                                    const std::vector<DeclaratorOp> &suffixes) {
  for (const DeclaratorOp &operation : suffixes) {
    if (operation.kind == DeclaratorOp::Kind::Array) {
      type = makeArray(std::move(type), operation.count);

      continue;
    }

    if (operation.kind == DeclaratorOp::Kind::Function) {
      auto function = makeType(TypeKind::Function);

      auto signature = std::make_shared<Signature>();

      signature->returns = std::move(type);

      signature->args = operation.arguments;

      signature->variadic = operation.variadic;

      function->functionSignature = std::move(signature);

      type = std::move(function);
    }
  }

  return type;
}

std::shared_ptr<Type> applyDeclarator(std::shared_ptr<Type> type, const Declarator &declarator) {
  type = applySuffixes(std::move(type), declarator.suffixes);

  if (declarator.nested) { type = applyDeclarator(std::move(type), *declarator.nested); }

  for (auto it = declarator.pointers.rbegin(); it != declarator.pointers.rend(); ++it) {
    type = makePointer(std::move(type), *it);
  }

  return type;
}

std::string declaratorName(const Declarator &declarator) {
  if (!declarator.name.empty()) { return declarator.name; }

  if (declarator.nested) { return declaratorName(*declarator.nested); }

  return {};
}

std::shared_ptr<Type> parseTypeName(const std::string &input, TypeContext *context) {
  std::string source = removeWhitespace(input);

  if (source.empty()) return nullptr;

  std::size_t qualifierPosition = 0;

  const Qualifiers baseQualifiers = parseQualifiers(source, qualifierPosition);

  source.erase(0, qualifierPosition);

  if (source.empty()) return nullptr;

  const std::size_t declaratorStart = source.find_first_of("*([");

  const std::string baseName =
      declaratorStart == std::string::npos ? source : source.substr(0, declaratorStart);

  const std::string declaratorSource =
      declaratorStart == std::string::npos ? std::string() : source.substr(declaratorStart);

  std::shared_ptr<Type> type = parseBaseType(baseName);

  if (!type) { type = parseStructType(baseName, context); }

  if (!type) return nullptr;

  type->isConst = baseQualifiers.isConst;

  type->isVolatile = baseQualifiers.isVolatile;

  type->isRestrict = baseQualifiers.isRestrict;

  if (declaratorSource.empty()) { return type; }

  Declarator declarator;

  DeclaratorParser parser(declaratorSource, context);

  if (!parser.parse(declarator)) { return nullptr; }

  return applyDeclarator(std::move(type), declarator);
}

std::shared_ptr<Type> parseParameterType(const std::string &input, TypeContext *context) {
  const std::string candidate = trim(input);

  if (candidate.empty()) return nullptr;

  std::shared_ptr<Type> type = parseTypeName(candidate, context);

  if (type) return type;

  std::size_t split = candidate.size();

  while (split > 0 && std::isspace(static_cast<unsigned char>(candidate[split - 1]))) { --split; }

  const std::size_t nameEnd = split;

  while (split > 0) {
    const char character = candidate[split - 1];

    if (std::isalnum(static_cast<unsigned char>(character)) || character == '_') {
      --split;
      continue;
    }

    break;
  }

  if (split == nameEnd) return nullptr;

  const std::string candidateType = trim(candidate.substr(0, split));

  if (candidateType.empty()) { return nullptr; }

  return parseTypeName(candidateType, context);
}

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
    if (!typeSource.empty()) { typeSource += " "; }

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

    if (character != ';') continue;

    if (parentheses != 0 || brackets != 0) { continue; }

    const std::string field = trim(source.substr(start, i - start));

    if (field.empty()) return false;

    fields.push_back(field);

    start = i + 1;
  }

  if (parentheses != 0 || brackets != 0) { return false; }

  const std::string trailing = trim(source.substr(start));

  return trailing.empty();
}

bool parseStructDefinition(const std::string &source, TypeContext *context, std::string &error) {
  error.clear();

  const std::string input = trim(source);

  if (input.size() < 9 || input.compare(0, 6, "struct") != 0) {
    error = "invalid struct definition";
    return false;
  }

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
      continue;
    }

    if (input[i] == '}') {
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

  const std::string body = input.substr(bodyStart, bodyEnd - bodyStart);

  std::vector<std::string> fieldSources;

  if (!splitStructFields(body, fieldSources)) {
    error = "invalid struct field list";
    return false;
  }

  if (fieldSources.empty()) {
    error = "struct must contain at least one field";
    return false;
  }

  auto structType = context->getOrCreateStruct(name);

  if (!structType) {
    error = "failed to create struct: " + name;
    return false;
  }

  std::vector<Field> fields;

  try {
    fields.reserve(fieldSources.size());
  } catch (...) {
    error = "struct field allocation failed";
    return false;
  }

  for (const std::string &fieldSource : fieldSources) {
    Field field;

    if (!parseFieldDeclaration(fieldSource, context, field, error)) { return false; }

    fields.push_back(std::move(field));
  }

  structType->fields = std::move(fields);

  return true;
}

bool parseFunctionDeclarationInternal(const std::string &source, ParsedFunction &function,
                                      std::string &error, TypeContext *context) {
  function = ParsedFunction{};
  error.clear();

  const std::string input = trim(source);

  if (input.empty()) {
    error = "empty function declaration";
    return false;
  }

  std::string returnType;
  std::string name;
  std::string argumentSource;

  if (!splitFunction(input, returnType, name, argumentSource)) {
    error = "invalid function declaration";
    return false;
  }

  std::shared_ptr<Type> parsedReturn = parseTypeName(returnType, context);

  if (!parsedReturn) {
    error = "unsupported return type: " + returnType;
    return false;
  }

  Signature signature;

  signature.returns = std::move(parsedReturn);

  std::vector<std::string> arguments;

  if (!argumentSource.empty()) {
    if (!splitArguments(argumentSource, arguments)) {
      error = "invalid function argument list";
      return false;
    }

    if (arguments.size() == 1 && trim(arguments[0]) == "void") { arguments.clear(); }
  }

  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string candidate = trim(arguments[i]);

    if (candidate == "...") {
      if (i + 1 != arguments.size()) {
        error = "variadic marker must be last";
        return false;
      }

      signature.variadic = true;
      continue;
    }

    if (candidate.empty()) {
      error = "empty function argument";
      return false;
    }

    std::shared_ptr<Type> argument = parseParameterType(candidate, context);

    if (!argument) {
      error = "unsupported function argument: " + candidate;
      return false;
    }

    signature.args.push_back(std::move(argument));
  }

  function.name = std::move(name);

  function.signature = std::move(signature);

  return true;
}

bool isStructDefinition(const std::string &source) {
  const std::string input = trim(source);

  if (input.size() < 6 || input.compare(0, 6, "struct") != 0) { return false; }

  std::size_t position = 6;

  if (position < input.size() && !std::isspace(static_cast<unsigned char>(input[position]))) {
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

  if (position == nameStart) { return false; }

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  return position < input.size() && input[position] == '{';
}

} // namespace

bool parseType(const std::string &source, std::shared_ptr<Type> &type, std::string &error,
               TypeContext *context) {
  type.reset();
  error.clear();

  const std::string input = trim(source);

  if (input.empty()) {
    error = "empty type";
    return false;
  }

  TypeContext localContext;

  if (!context) { context = &localContext; }

  type = parseTypeName(input, context);

  if (!type) {
    error = "unsupported type: " + input;
    return false;
  }

  return true;
}

bool parseFunctionDeclaration(const std::string &source, ParsedFunction &function,
                              std::string &error) {
  TypeContext context;

  return parseFunctionDeclarationInternal(source, function, error, &context);
}

bool parseCSource(const std::string &source, ParsedCSource &result, std::string &error) {
  result = ParsedCSource{};
  error.clear();

  const std::string normalizedSource = removePreprocessorDirectives(source);

  auto context = std::make_shared<TypeContext>();

  std::size_t start = 0;
  std::size_t parentheses = 0;
  std::size_t braces = 0;

  for (std::size_t i = 0; i <= normalizedSource.size(); ++i) {
    const bool end = i == normalizedSource.size();

    const char character = end ? ';' : normalizedSource[i];

    if (!end) {
      if (character == '(') {
        ++parentheses;
        continue;
      }

      if (character == ')') {
        if (parentheses == 0) {
          error = "unmatched ')'";
          return false;
        }

        --parentheses;
        continue;
      }

      if (character == '{') {
        ++braces;
        continue;
      }

      if (character == '}') {
        if (braces == 0) {
          error = "unmatched '}'";
          return false;
        }

        --braces;
        continue;
      }
    }

    if (end || (character == ';' && parentheses == 0 && braces == 0)) {
      const std::string declaration = trim(normalizedSource.substr(start, i - start));

      start = i + 1;

      if (declaration.empty()) { continue; }

      if (isStructDefinition(declaration)) {
        if (!parseStructDefinition(declaration, context.get(), error)) { return false; }

        continue;
      }

      if (declaration.find('(') == std::string::npos) { continue; }

      ParsedFunction function;

      if (!parseFunctionDeclarationInternal(declaration, function, error, context.get())) {
        return false;
      }

      result.functions.push_back(std::move(function));
    }
  }

  if (parentheses != 0) {
    error = "unmatched '('";
    return false;
  }

  if (braces != 0) {
    error = "unmatched '{'";
    return false;
  }

  result.context = std::move(context);

  return true;
}

}} // namespace edon::ffi
