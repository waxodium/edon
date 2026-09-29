#include "runtime_output.hpp"
#include <JavaScriptCore/JavaScript.h>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace edon {
namespace {

constexpr const char *COLOR_RESET = "\033[0m";
constexpr const char *COLOR_KEYWORD = "\033[35m";
constexpr const char *COLOR_IDENTIFIER = "\033[36m";
constexpr const char *COLOR_STRING = "\033[32m";
constexpr const char *COLOR_NUMBER = "\033[33m";
constexpr const char *COLOR_COMMENT = "\033[90m";
constexpr const char *COLOR_OPERATOR = "\033[36m";
constexpr const char *COLOR_ERROR = "\033[31m";
constexpr const char *COLOR_PATH = "\033[36m";
constexpr const char *COLOR_LINE = "\033[90m";

struct SourceLocation {
    std::string filename;
    std::size_t line = 0;
    std::size_t column = 0;
};

std::string jsStringToUTF8(JSStringRef string) {
    if (!string) return {};
    const std::size_t maxSize = JSStringGetMaximumUTF8CStringSize(string);
    if (maxSize == 0) return {};
    std::string result(maxSize, '\0');
    const std::size_t len = JSStringGetUTF8CString(string, result.data(), maxSize);
    if (len == 0) return {};
    result.resize(len - 1);
    return result;
}

std::string valueToString(JSContextRef context, JSValueRef value) {
    if (!value) return {};
    JSValueRef error = nullptr;
    JSStringRef string = JSValueToStringCopy(context, value, &error);
    if (!string) return {};
    std::string result = jsStringToUTF8(string);
    JSStringRelease(string);
    return result;
}

std::string getStringProperty(JSContextRef context, JSObjectRef object, const char *name) {
    if (!object || !name) return {};
    JSStringRef property = JSStringCreateWithUTF8CString(name);
    if (!property) return {};
    JSValueRef error = nullptr;
    JSValueRef value = JSObjectGetProperty(context, object, property, &error);
    JSStringRelease(property);
    if (error || !value || !JSValueIsString(context, value)) return {};
    return valueToString(context, value);
}

std::size_t getNumberProperty(JSContextRef context, JSObjectRef object, const char *name) {
    if (!object || !name) return 0;
    JSStringRef property = JSStringCreateWithUTF8CString(name);
    if (!property) return 0;
    JSValueRef error = nullptr;
    JSValueRef value = JSObjectGetProperty(context, object, property, &error);
    JSStringRelease(property);
    if (error || !value || !JSValueIsNumber(context, value)) return 0;
    double number = JSValueToNumber(context, value, &error);
    if (error || number <= 0 || number != static_cast<double>(static_cast<std::size_t>(number))) return 0;
    return static_cast<std::size_t>(number);
}

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool parseUnsigned(const std::string& value, std::size_t& result) {
    if (value.empty()) return false;
    for (char character : value) {
        if (!std::isdigit(static_cast<unsigned char>(character))) return false;
    }
    char *end = nullptr;
    unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed > static_cast<unsigned long long>(static_cast<std::size_t>(-1))) return false;
    result = static_cast<std::size_t>(parsed);
    return true;
}

bool parseLocationSuffix(const std::string& value, SourceLocation& location) {
    if (value.empty()) return false;
    const std::size_t lastColon = value.rfind(':');
    if (lastColon == std::string::npos) return false;
    const std::size_t prevColon = value.rfind(':', lastColon - 1);
    if (prevColon == std::string::npos || prevColon + 1 >= lastColon || lastColon + 1 >= value.size()) return false;

    const std::string filename = value.substr(0, prevColon);
    const std::string lineText = value.substr(prevColon + 1, lastColon - prevColon - 1);
    const std::string columnText = value.substr(lastColon + 1);

    std::size_t line = 0, column = 0;
    if (!parseUnsigned(lineText, line) || !parseUnsigned(columnText, column) || filename.empty() || line == 0) return false;

    location.filename = filename;
    location.line = line;
    location.column = column;
    return true;
}

bool parseStackLocation(const std::string& stack, SourceLocation& location) {
    std::istringstream stream(stack);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.rfind("at ", 0) != 0) continue;
        std::string frame = trim(line.substr(3));
        SourceLocation candidate;
        if (parseLocationSuffix(frame, candidate)) {
            location = candidate;
            return true;
        }
    }
    return false;
}

std::string sourceLine(const std::string& source, std::size_t lineNumber) {
    if (lineNumber == 0) return {};
    std::size_t currentLine = 1, start = 0;
    while (start <= source.size()) {
        const std::size_t end = source.find('\n', start);
        if (currentLine == lineNumber) {
            std::string line = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (end == std::string::npos) break;
        start = end + 1;
        ++currentLine;
    }
    return {};
}

bool isIdentifierStart(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_' || character == '$';
}

bool isIdentifierPart(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '$';
}

bool isKeyword(const std::string& word) {
    static const char *keywords[] = {
        "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
        "default", "delete", "do", "else", "export", "extends", "false", "finally", "for", "from",
        "function", "get", "if", "import", "in", "instanceof", "let", "new", "null", "of",
        "return", "set", "static", "super", "switch", "this", "throw", "true", "try", "typeof",
        "var", "void", "while", "with", "yield"
    };
    for (const char *keyword : keywords) {
        if (word == keyword) return true;
    }
    return false;
}

std::string highlightSourceLine(const std::string& line) {
    std::ostringstream output;
    std::size_t position = 0;
    while (position < line.size()) {
        char character = line[position];

        if (character == '/' && position + 1 < line.size() && line[position + 1] == '/') {
            output << COLOR_COMMENT << line.substr(position) << COLOR_RESET;
            break;
        }

        if (character == '/' && position + 1 < line.size() && line[position + 1] == '*') {
            const std::size_t end = line.find("*/", position + 2);
            if (end == std::string::npos) {
                output << COLOR_COMMENT << line.substr(position) << COLOR_RESET;
                break;
            }
            output << COLOR_COMMENT << line.substr(position, end + 2 - position) << COLOR_RESET;
            position = end + 2;
            continue;
        }

        if (character == '\'' || character == '"' || character == '`') {
            char quote = character;
            std::size_t start = position++;
            bool escaped = false;
            while (position < line.size()) {
                char current = line[position];
                if (escaped) { escaped = false; ++position; continue; }
                if (current == '\\') { escaped = true; ++position; continue; }
                if (current == quote) { ++position; break; }
                ++position;
            }
            output << COLOR_STRING << line.substr(start, position - start) << COLOR_RESET;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(character))) {
            std::size_t start = position;
            while (position < line.size()) {
                char current = line[position];
                if (!std::isalnum(static_cast<unsigned char>(current)) && current != '.' && current != '_') break;
                ++position;
            }
            output << COLOR_NUMBER << line.substr(start, position - start) << COLOR_RESET;
            continue;
        }

        if (isIdentifierStart(character)) {
            std::size_t start = position++;
            while (position < line.size() && isIdentifierPart(line[position])) ++position;
            std::string word = line.substr(start, position - start);
            output << (isKeyword(word) ? COLOR_KEYWORD : COLOR_IDENTIFIER) << word << COLOR_RESET;
            continue;
        }

        static const std::string operators[] = {
            "===", "!==", ">>>", "**=", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "++", "--",
            "+=", "-=", "*=", "/=", "%=", "**", "<<", ">>", "&=", "|=", "^=", "+", "-", "*", "/", "%",
            "=", "<", ">", "!", "&", "|", "^", "~", "?"
        };

        bool matched = false;
        for (const auto& op : operators) {
            if (line.compare(position, op.size(), op) == 0) {
                output << COLOR_OPERATOR << op << COLOR_RESET;
                position += op.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;

        output << character;
        ++position;
    }
    return output.str();
}

std::string resolvePath(const std::string& reportedPath, const std::string& sourceFilename) {
    if (reportedPath.empty()) return reportedPath;
    std::filesystem::path reported(reportedPath);
    if (reported.is_absolute()) return reported.lexically_normal().string();

    std::filesystem::path sourcePath(sourceFilename);
    std::error_code error;
    if (sourcePath.is_relative()) {
        auto absolute = std::filesystem::absolute(sourcePath, error);
        if (!error) sourcePath = absolute;
    }

    auto currentDirectory = sourcePath.has_parent_path() ? sourcePath.parent_path() : std::filesystem::current_path(error);
    if (!error && std::filesystem::exists(currentDirectory / reported)) {
        return (currentDirectory / reported).lexically_normal().string();
    }

    auto workDirectory = std::filesystem::current_path(error);
    if (!error && std::filesystem::exists(workDirectory / reported)) {
        return (workDirectory / reported).lexically_normal().string();
    }

    auto directory = currentDirectory;
    while (!directory.empty()) {
        if (std::filesystem::exists(directory / reported)) {
            return (directory / reported).lexically_normal().string();
        }
        auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    return reported.lexically_normal().string();
}

bool extractReferenceErrorIdentifier(const std::string& message, std::string& identifier) {
    const std::string prefix = "Can't find variable:";
    const std::size_t position = message.find(prefix);
    if (position == std::string::npos) return false;
    identifier = trim(message.substr(position + prefix.size()));
    if (identifier.empty() || !isIdentifierStart(identifier[0])) return false;
    for (std::size_t i = 1; i < identifier.size(); ++i) {
        if (!isIdentifierPart(identifier[i])) return false;
    }
    return true;
}

bool isIdentifierAt(const std::string& line, std::size_t position, const std::string& identifier) {
    if (identifier.empty() || position + identifier.size() > line.size()) return false;
    if (line.compare(position, identifier.size(), identifier) != 0) return false;
    bool validStart = position == 0 || !isIdentifierPart(line[position - 1]);
    std::size_t end = position + identifier.size();
    bool validEnd = end >= line.size() || !isIdentifierPart(line[end]);
    return validStart && validEnd;
}

std::vector<std::size_t> findIdentifierPositions(const std::string& line, const std::string& identifier) {
    std::vector<std::size_t> positions;
    if (identifier.empty()) return positions;
    std::size_t position = 0;
    while (position < line.size()) {
        position = line.find(identifier, position);
        if (position == std::string::npos) break;
        if (isIdentifierAt(line, position, identifier)) positions.push_back(position);
        ++position;
    }
    return positions;
}

bool findReferenceErrorColumn(const std::string& message, const std::string& line, std::size_t reportedColumn, std::size_t& column) {
    std::string identifier;
    if (!extractReferenceErrorIdentifier(message, identifier)) return false;
    auto positions = findIdentifierPositions(line, identifier);
    if (positions.empty()) return false;

    if (reportedColumn > 0) {
        std::size_t bestPosition = positions.front();
        std::size_t bestDistance = static_cast<std::size_t>(-1);
        for (auto position : positions) {
            std::size_t candidateColumn = position + 1;
            std::size_t candidateEnd = candidateColumn + identifier.size();
            std::size_t distance = candidateEnd > reportedColumn ? candidateEnd - reportedColumn : reportedColumn - candidateEnd;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPosition = position;
            }
            if (candidateEnd == reportedColumn) {
                bestPosition = position;
                break;
            }
        }
        column = bestPosition + 1;
        return true;
    }

    if (positions.size() != 1) return false;
    column = positions.front() + 1;
    return true;
}

bool findSyntaxErrorColumn(const std::string& message, const std::string& line, std::size_t& column) {
    const std::string prefix = "Unexpected token '";
    const std::size_t start = message.find(prefix);
    if (start == std::string::npos) return false;
    const std::size_t tokenStart = start + prefix.size();
    const std::size_t tokenEnd = message.find('\'', tokenStart);
    if (tokenEnd == std::string::npos || tokenEnd == tokenStart) return false;

    const std::string token = message.substr(tokenStart, tokenEnd - tokenStart);
    if (token.empty()) return false;
    const std::size_t position = line.find(token);
    if (position == std::string::npos) return false;
    column = position + 1;
    return true;
}

std::size_t visualColumn(const std::string& line, std::size_t sourceColumn) {
    if (sourceColumn <= 1) return 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i + 1 < sourceColumn && i < line.size(); ++i) {
        if (line[i] == '\t') column += 8 - ((column - 1) % 8);
        else ++column;
    }
    return column;
}

bool parseCompilerDiagnostic(const std::string& message, std::size_t& line, std::string& text) {
    const std::size_t firstColon = message.find(':');
    if (firstColon == std::string::npos) return false;
    const std::size_t secondColon = message.find(':', firstColon + 1);
    if (secondColon == std::string::npos) return false;

    std::string lineText = message.substr(firstColon + 1, secondColon - firstColon - 1);
    if (!parseUnsigned(lineText, line)) return false;

    std::size_t textStart = secondColon + 1;
    while (textStart < message.size() && std::isspace(static_cast<unsigned char>(message[textStart]))) ++textStart;
    text = message.substr(textStart);
    return line > 0;
}

void printSourceContext(const std::string& source, const SourceLocation& location) {
    if (location.line == 0) return;
    std::string current = sourceLine(source, location.line);
    if (current.empty() && location.line > source.size()) return;

    std::size_t firstLine = location.line > 2 ? location.line - 2 : 1;
    std::size_t width = std::to_string(location.line).size();

    for (std::size_t line = firstLine; line <= location.line; ++line) {
        std::string lineString = sourceLine(source, line);
        std::cerr << COLOR_LINE << std::string(width - std::to_string(line).size(), ' ') << line << COLOR_RESET << " | " << highlightSourceLine(lineString) << '\n';

        if (line == location.line && location.column > 0) {
            std::size_t caretColumn = visualColumn(lineString, location.column);
            std::cerr << std::string(width, ' ') << " | " << std::string(caretColumn - 1, ' ') << COLOR_ERROR << '^' << COLOR_RESET << '\n';
        }
    }
}

} // namespace

void printCompilerDiagnostic(const std::string& source, const std::string& diagnostic) {
    if (diagnostic.empty()) return;
    std::istringstream stream(diagnostic);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::size_t compilerLine = 0;
        std::string compilerMessage;
        if (!parseCompilerDiagnostic(line, compilerLine, compilerMessage)) {
            std::cerr << line << '\n';
            continue;
        }
        std::string cLine = sourceLine(source, compilerLine);
        if (!cLine.empty()) {
            std::cerr << COLOR_LINE << compilerLine << COLOR_RESET << " | " << cLine << '\n';
        }
        std::cerr << COLOR_ERROR << compilerMessage << COLOR_RESET << '\n';
    }
}

void printDiagnostic(const std::string& source, const std::string& filename, JSContextRef context, JSValueRef error) {
    if (!context || !error) return;

    JSObjectRef errorObject = JSValueToObject(context, error, nullptr);
    std::string errorName, message, stack;

    if (errorObject) {
        errorName = getStringProperty(context, errorObject, "name");
        message = getStringProperty(context, errorObject, "message");
        stack = getStringProperty(context, errorObject, "stack");
    }

    if (errorName.empty()) errorName = "Error";
    if (message.empty()) message = valueToString(context, error);

    SourceLocation location;
    if (errorObject) {
        location.line = getNumberProperty(context, errorObject, "line");
        location.column = getNumberProperty(context, errorObject, "column");
        location.filename = getStringProperty(context, errorObject, "sourceURL");
        if (location.filename.empty()) {
            location.filename = getStringProperty(context, errorObject, "sourceURLString");
        }
    }

    SourceLocation stackLocation;
    if (parseStackLocation(stack, stackLocation)) {
        if (location.filename.empty()) location.filename = stackLocation.filename;
        if (location.line == 0) location.line = stackLocation.line;
        if (location.column == 0) location.column = stackLocation.column;
    }

    if (location.filename.empty()) location.filename = filename;
    if (location.filename.empty()) location.filename = "<anonymous>";
    location.filename = resolvePath(location.filename, filename);

    if (errorName == "ReferenceError" && location.line > 0) {
        std::string line = sourceLine(source, location.line);
        std::size_t referenceColumn = 0;
        if (findReferenceErrorColumn(message, line, location.column, referenceColumn)) {
            location.column = referenceColumn;
        }
    }

    if (errorName == "SyntaxError" && location.line > 0 && location.column == 0) {
        std::string line = sourceLine(source, location.line);
        std::size_t syntaxColumn = 0;
        if (findSyntaxErrorColumn(message, line, syntaxColumn)) {
            location.column = syntaxColumn;
        }
    }

    printSourceContext(source, location);
    std::cerr << '\n';
    std::cerr << COLOR_ERROR << errorName << COLOR_RESET << ": " << message << '\n';
    std::cerr << '\t' << "at " << COLOR_PATH << location.filename << COLOR_RESET;

    if (location.line > 0) {
        std::cerr << ':' << location.line;
        if (location.column > 0) {
            std::cerr << ':' << location.column;
        }
    }
    std::cerr << '\n';

    if (errorObject) {
        std::string compilerError = getStringProperty(context, errorObject, "compilerError");
        std::string compilerSource = getStringProperty(context, errorObject, "compilerSource");
        if (!compilerError.empty()) {
            std::cerr << '\n';
            printCompilerDiagnostic(compilerSource, compilerError);
        }
    }
}

} // namespace edon
