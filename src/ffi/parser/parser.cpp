#include "parser.hpp"
#include "ffi/signature.hpp"

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
  std::vector<std::string> parameters;
  bool emptyParameterList = false;
  bool voidParameterList = false;
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
  if (name == "bool" || name == "_Bool") return makeType(TypeKind::Bool);
  if (name == "char" || name == "signedchar") return makeType(TypeKind::Char);
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

std::shared_ptr<Type> parseStructType(const std::string &name, TypeContext *context) {
  if (name.size() <= 6 || name.compare(0, 6, "struct") != 0 || !context) { return nullptr; }

  const std::string structName = name.substr(6);
  return structName.empty() ? nullptr : context->getOrCreateStruct(structName);
}

std::shared_ptr<Type> parseUnionType(const std::string &name, TypeContext *context) {
  if (name.size() <= 5 || name.compare(0, 5, "union") != 0 || !context) { return nullptr; }

  const std::string unionName = name.substr(5);
  return unionName.empty() ? nullptr : context->getOrCreateUnion(unionName);
}

std::shared_ptr<Type> parseEnumType(const std::string &name, TypeContext *context) {
  if (name.size() <= 4 || name.compare(0, 4, "enum") != 0 || !context) { return nullptr; }

  const std::string enumName = name.substr(4);
  return enumName.empty() ? nullptr : context->getOrCreateEnum(enumName);
}

bool splitArguments(const std::string &source, std::vector<std::string> &arguments) {
  arguments.clear();

  if (source.empty()) { return true; }

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

    if (character != ',' || parentheses != 0 || brackets != 0) { continue; }

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

std::shared_ptr<Type> cloneTypeForQualifiers(const std::shared_ptr<Type> &source) {
  if (!source) return nullptr;

  std::shared_ptr<Type> result;

  switch (source->kind) {
  case TypeKind::Pointer:
    result = makeType(TypeKind::Pointer);
    result->element = source->element;
    break;

  case TypeKind::Array: result = makeArray(source->element, source->count); break;

  case TypeKind::Function:
    result = makeType(TypeKind::Function);
    result->functionSignature = source->functionSignature;
    result->complete = source->complete;
    break;

  case TypeKind::Struct: result = makeStruct(source->name, source->fields); break;

  case TypeKind::Union: result = makeUnion(source->name, source->fields); break;

  case TypeKind::Enum: result = makeEnum(source->name, source->enumValues); break;

  default: result = makeType(source->kind); break;
  }

  if (!result) return nullptr;

  result->name = source->name;
  result->size = source->size;
  result->alignment = source->alignment;
  result->complete = source->complete && isPrimitive(source->kind);
  result->isConst = source->isConst;
  result->isVolatile = source->isVolatile;
  result->isRestrict = source->isRestrict;

  return result;
}

class DeclaratorParser {
public:
  explicit DeclaratorParser(const std::string &source) : source_(source) {}

  bool parse(Declarator &declarator) {
    position_ = 0;
    return parseDeclarator(declarator) && position_ == source_.size();
  }

private:
  bool parseIdentifier(std::string &name) {
    if (position_ >= source_.size()) return false;

    const unsigned char first = static_cast<unsigned char>(source_[position_]);

    if (!std::isalpha(first) && source_[position_] != '_') { return false; }

    const std::size_t start = position_++;

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

      if (!parseDeclarator(*nested) || !consume(')')) { return false; }

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

        if (position_ >= source_.size() || source_[position_] == ']') { return false; }

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

        std::vector<std::string> parameters;

        if (!splitArguments(argumentSource, parameters)) { return false; }

        function.emptyParameterList = argumentSource.empty();

        if (parameters.size() == 1 && parameters[0] == "void") {
          function.voidParameterList = true;
          suffixes.push_back(std::move(function));
          continue;
        }

        for (std::size_t i = 0; i < parameters.size(); ++i) {
          const std::string parameter = trim(parameters[i]);

          if (parameter == "...") {
            if (i + 1 != parameters.size() || i == 0) return false;
            function.variadic = true;
            continue;
          }

          function.parameters.push_back(parameter);
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
                                    const std::vector<DeclaratorOp> &suffixes,
                                    TypeContext *context) {
  for (const DeclaratorOp &operation : suffixes) {
    if (operation.kind == DeclaratorOp::Kind::Array) {
      type = makeArray(std::move(type), operation.count);
      continue;
    }

    if (operation.kind == DeclaratorOp::Kind::Function) {
      auto function = makeType(TypeKind::Function);
      auto signature = std::make_shared<Signature>();

      signature->returns = std::move(type);

      for (const std::string &parameter : operation.parameters) {
        auto argument = parseParameterType(parameter, context);
        if (!argument) return nullptr;
        signature->args.push_back(std::move(argument));
      }

      signature->variadic = operation.variadic;

      function->functionSignature = std::move(signature);
      type = std::move(function);
    }
  }

  return type;
}

std::shared_ptr<Type> applyDeclarator(std::shared_ptr<Type> type, const Declarator &declarator,
                                      TypeContext *context) {
  type = applySuffixes(std::move(type), declarator.suffixes, context);
  if (!type) return nullptr;

  if (declarator.nested) {
    type = applyDeclarator(std::move(type), *declarator.nested, context);
    if (!type) return nullptr;
  }

  for (auto it = declarator.pointers.rbegin(); it != declarator.pointers.rend(); ++it) {
    type = makePointer(std::move(type), *it);
  }

  return type;
}

std::string declaratorName(const Declarator &declarator) {
  if (!declarator.name.empty()) return declarator.name;
  if (declarator.nested) return declaratorName(*declarator.nested);
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

  if (!type) type = parseStructType(baseName, context);
  if (!type) type = parseUnionType(baseName, context);
  if (!type) type = parseEnumType(baseName, context);
  if (!type && context) type = context->findTypedef(baseName);

  if (!type) return nullptr;

  if (baseQualifiers.isConst || baseQualifiers.isVolatile || baseQualifiers.isRestrict) {
    type = cloneTypeForQualifiers(type);
    if (!type) return nullptr;

    type->isConst = baseQualifiers.isConst;
    type->isVolatile = baseQualifiers.isVolatile;
    type->isRestrict = baseQualifiers.isRestrict;
  }

  if (declaratorSource.empty()) return type;

  Declarator declarator;
  DeclaratorParser parser(declaratorSource);

  if (!parser.parse(declarator)) return nullptr;

  return applyDeclarator(std::move(type), declarator, context);
}

std::shared_ptr<Type> parseParameterType(const std::string &input, TypeContext *context) {
  const std::string candidate = trim(input);
  if (candidate.empty()) return nullptr;

  auto type = parseTypeName(candidate, context);
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
  if (candidateType.empty()) return nullptr;

  return parseTypeName(candidateType, context);
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

  auto parsedReturn = parseTypeName(returnType, context);

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

    auto argument = parseParameterType(candidate, context);

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

enum class AggregateKind { None, Struct, Union, Enum };

AggregateKind classifyAggregateDefinition(const std::string &source) {
  const std::string input = trim(source);

  AggregateKind kind = AggregateKind::None;
  std::size_t keywordLength = 0;

  if (input.compare(0, 6, "struct") == 0) {
    kind = AggregateKind::Struct;
    keywordLength = 6;
  } else if (input.compare(0, 5, "union") == 0) {
    kind = AggregateKind::Union;
    keywordLength = 5;
  } else if (input.compare(0, 4, "enum") == 0) {
    kind = AggregateKind::Enum;
    keywordLength = 4;
  } else {
    return AggregateKind::None;
  }

  std::size_t position = keywordLength;

  if (position >= input.size() || !std::isspace(static_cast<unsigned char>(input[position]))) {
    return AggregateKind::None;
  }

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  const std::size_t nameStart = position;

  while (position < input.size()) {
    const unsigned char character = static_cast<unsigned char>(input[position]);

    if (!std::isalnum(character) && character != '_') { break; }

    ++position;
  }

  if (position == nameStart ||
      (!std::isalpha(static_cast<unsigned char>(input[nameStart])) && input[nameStart] != '_')) {
    return AggregateKind::None;
  }

  while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
    ++position;
  }

  return position < input.size() && input[position] == '{' ? kind : AggregateKind::None;
}

bool parseTypedefDeclaration(const std::string &source, TypeContext *context, std::string &error) {
  error.clear();

  const std::string input = trim(source);

  if (input.size() < 7 || input.compare(0, 7, "typedef") != 0 ||
      (input.size() > 7 && !std::isspace(static_cast<unsigned char>(input[7])))) {
    error = "invalid typedef declaration";
    return false;
  }

  if (!context) {
    error = "missing type context";
    return false;
  }

  ContextTransaction transaction(context);

  const std::string body = trim(input.substr(7));

  if (body.empty()) {
    error = "typedef requires a type and name";
    return false;
  }

  auto parseAliasDeclarators = [&](const std::string &sourceText,
                                   std::vector<Declarator> &declarators,
                                   std::vector<std::string> &names) -> bool {
    std::vector<std::string> parts;

    if (!splitArguments(sourceText, parts) || parts.empty()) {
      error = "invalid typedef declarator list";
      return false;
    }

    declarators.clear();
    names.clear();
    declarators.reserve(parts.size());
    names.reserve(parts.size());

    for (const std::string &part : parts) {
      Declarator declarator;
      DeclaratorParser parser(part);

      if (!parser.parse(declarator)) {
        error = "invalid typedef declarator: " + part;
        return false;
      }

      const std::string name = declaratorName(declarator);

      if (name.empty()) {
        error = "typedef declarator requires a name";
        return false;
      }

      for (const std::string &existing : names) {
        if (existing == name) {
          error = "duplicate typedef name: " + name;
          return false;
        }
      }

      if (context->findTypedef(name)) {
        error = "duplicate typedef: " + name;
        return false;
      }

      names.push_back(name);
      declarators.push_back(std::move(declarator));
    }

    return true;
  };

  const std::size_t open = body.find('{');

  if (open != std::string::npos) {
    std::size_t depth = 0;
    std::size_t close = std::string::npos;

    for (std::size_t i = open; i < body.size(); ++i) {
      if (body[i] == '{') {
        ++depth;
      } else if (body[i] == '}') {
        if (depth == 0) { break; }

        --depth;

        if (depth == 0) {
          close = i;
          break;
        }
      }
    }

    if (close == std::string::npos) {
      error = "unterminated typedef aggregate definition";
      return false;
    }

    const std::string definition = trim(body.substr(0, close + 1));
    const std::string aliasSource = trim(body.substr(close + 1));
    const AggregateKind kind = classifyAggregateDefinition(definition);

    if (kind != AggregateKind::None) {
      std::vector<Declarator> declarators;
      std::vector<std::string> names;

      if (!parseAliasDeclarators(aliasSource, declarators, names)) { return false; }

      std::size_t keywordLength = 0;

      switch (kind) {
      case AggregateKind::Struct: keywordLength = 6; break;
      case AggregateKind::Union: keywordLength = 5; break;
      case AggregateKind::Enum: keywordLength = 4; break;
      case AggregateKind::None: return false;
      }

      std::size_t nameStart = keywordLength;

      while (nameStart < definition.size() &&
             std::isspace(static_cast<unsigned char>(definition[nameStart]))) {
        ++nameStart;
      }

      std::size_t nameEnd = nameStart;

      while (nameEnd < definition.size() &&
             (std::isalnum(static_cast<unsigned char>(definition[nameEnd])) ||
              definition[nameEnd] == '_')) {
        ++nameEnd;
      }

      if (nameEnd == nameStart) {
        error = "aggregate typedef requires a tag name";
        return false;
      }

      const std::string tagName = definition.substr(nameStart, nameEnd - nameStart);

      std::shared_ptr<Type> aliasProbe;

      switch (kind) {
      case AggregateKind::Struct:
        aliasProbe = context->findStruct(tagName);
        if (!aliasProbe) aliasProbe = makeStruct(tagName, {});
        break;

      case AggregateKind::Union:
        aliasProbe = context->findUnion(tagName);
        if (!aliasProbe) aliasProbe = makeUnion(tagName, {});
        break;

      case AggregateKind::Enum:
        aliasProbe = context->findEnum(tagName);
        if (!aliasProbe) aliasProbe = makeEnum(tagName, {});
        break;

      case AggregateKind::None: return false;
      }

      if (!aliasProbe) {
        error = "failed to prepare aggregate typedef: " + tagName;
        return false;
      }

      for (std::size_t i = 0; i < declarators.size(); ++i) {
        if (!applyDeclarator(aliasProbe, declarators[i], context)) {
          error = "invalid typedef type: " + names[i];
          return false;
        }
      }

      std::shared_ptr<Type> aggregate;

      switch (kind) {
      case AggregateKind::Struct: {
        const auto existing = context->findStruct(tagName);

        if (existing && existing->complete) {
          error = "struct already defined: " + tagName;
          return false;
        }

        if (!parseStructDefinition(definition, context, error)) { return false; }

        aggregate = context->findStruct(tagName);
        break;
      }

      case AggregateKind::Union: {
        const auto existing = context->findUnion(tagName);

        if (existing && existing->complete) {
          error = "union already defined: " + tagName;
          return false;
        }

        if (!parseUnionDefinition(definition, context, error)) { return false; }

        aggregate = context->findUnion(tagName);
        break;
      }

      case AggregateKind::Enum: {
        const auto existing = context->findEnum(tagName);

        if (existing && existing->complete) {
          error = "enum already defined: " + tagName;
          return false;
        }

        if (!parseEnumDefinition(definition, context, error)) { return false; }

        aggregate = context->findEnum(tagName);
        break;
      }

      case AggregateKind::None: return false;
      }

      if (!aggregate) {
        error = "failed to resolve aggregate typedef: " + tagName;
        return false;
      }

      std::vector<std::pair<std::string, std::shared_ptr<Type>>> aliases;
      aliases.reserve(declarators.size());

      for (std::size_t i = 0; i < declarators.size(); ++i) {
        auto aliasType = applyDeclarator(aggregate, declarators[i], context);

        if (!aliasType) {
          error = "invalid typedef type: " + names[i];
          return false;
        }

        aliases.emplace_back(names[i], std::move(aliasType));
      }

      if (!context->defineTypedefs(std::move(aliases))) {
        error = "failed to register aggregate typedef aliases";
        return false;
      }

      transaction.commit();
      return true;
    }
  }

  for (std::size_t i = body.size(); i > 0; --i) {
    const std::size_t boundary = i - 1;

    if (!std::isspace(static_cast<unsigned char>(body[boundary]))) { continue; }

    const std::string typeSource = trim(body.substr(0, boundary));
    const std::string declaratorSource = trim(body.substr(boundary + 1));

    if (typeSource.empty() || declaratorSource.empty()) { continue; }

    ContextTransaction candidateTransaction(context);

    auto baseType = parseTypeName(typeSource, context);
    if (!baseType) continue;

    std::vector<Declarator> declarators;
    std::vector<std::string> names;

    error.clear();

    if (!parseAliasDeclarators(declaratorSource, declarators, names)) { continue; }

    std::vector<std::pair<std::string, std::shared_ptr<Type>>> aliases;
    aliases.reserve(declarators.size());

    bool valid = true;

    for (std::size_t j = 0; j < declarators.size(); ++j) {
      auto aliasType = applyDeclarator(baseType, declarators[j], context);

      if (!aliasType) {
        valid = false;
        break;
      }

      aliases.emplace_back(names[j], std::move(aliasType));
    }

    if (!valid) continue;

    if (!context->defineTypedefs(std::move(aliases))) {
      error = "failed to register typedef aliases";
      return false;
    }

    candidateTransaction.commit();
    transaction.commit();
    return true;
  }

  error = "invalid typedef declaration: " + body;
  return false;
}

bool parseDeclaration(const std::string &declaration, TypeContext *context, ParsedCSource &result,
                      std::string &error) {
  if (declaration.empty()) { return true; }

  if (declaration.compare(0, 7, "typedef") == 0 &&
      (declaration.size() == 7 || std::isspace(static_cast<unsigned char>(declaration[7])))) {
    return parseTypedefDeclaration(declaration, context, error);
  }

  switch (classifyAggregateDefinition(declaration)) {
  case AggregateKind::Struct: return parseStructDefinition(declaration, context, error);
  case AggregateKind::Union: return parseUnionDefinition(declaration, context, error);
  case AggregateKind::Enum: return parseEnumDefinition(declaration, context, error);
  case AggregateKind::None: break;
  }

  if (declaration.find('{') == std::string::npos && declaration.find('(') == std::string::npos) {
    const std::size_t separator = declaration.find_first_of(" \t\r\n");

    if (separator != std::string::npos) {
      const std::string keyword = declaration.substr(0, separator);
      const std::string tag = trim(declaration.substr(separator + 1));

      const bool validTag =
          !tag.empty() &&
          (std::isalpha(static_cast<unsigned char>(tag.front())) || tag.front() == '_') &&
          std::all_of(tag.begin() + 1, tag.end(), [](char character) {
            return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
          });

      if (validTag) {
        if (keyword == "struct") {
          if (!context->getOrCreateStruct(tag)) {
            error = "failed to declare struct: " + tag;
            return false;
          }

          return true;
        }

        if (keyword == "union") {
          if (!context->getOrCreateUnion(tag)) {
            error = "failed to declare union: " + tag;
            return false;
          }

          return true;
        }

        if (keyword == "enum") {
          if (!context->getOrCreateEnum(tag)) {
            error = "failed to declare enum: " + tag;
            return false;
          }

          return true;
        }
      }
    }
  }

  if (declaration.find('(') == std::string::npos) { return true; }

  ParsedFunction function;

  if (!parseFunctionDeclarationInternal(declaration, function, error, context)) { return false; }

  result.functions.push_back(std::move(function));
  return true;
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
      const std::string rawDeclaration = normalizedSource.substr(start, i - start);
      const std::string declaration = trim(rawDeclaration);
      const std::size_t leading = rawDeclaration.find_first_not_of(" \t\r\n");
      const std::size_t declarationStart = leading == std::string::npos ? i : start + leading;

      start = i + 1;

      if (!parseDeclaration(declaration, context.get(), result, error)) {
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t location = declarationStart;

        for (std::size_t offset = 0; offset < location; ++offset) {
          if (normalizedSource[offset] == '\n') {
            ++line;
            column = 1;
          } else {
            ++column;
          }
        }

        error = "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " +
                (error.empty() ? "invalid declaration" : error);
        return false;
      }
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
