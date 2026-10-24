#include "message_format.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <variant>

namespace nolf {
namespace {
constexpr std::size_t maxOutput = 65534;
enum class Kind { none, text, signedInt, unsignedInt, pointer };
struct Token {
    std::string text;
    unsigned index = 0;
    Kind kind = Kind::none;
};
struct Value {
    const char* text = nullptr;
    int signedInt = 0;
    unsigned unsignedInt = 0;
    void* pointer = nullptr;
};
Kind kind(std::string_view spec) {
    if (spec.empty()) return Kind::text;
    std::size_t pos = 0;
    while (pos < spec.size() && std::string_view("-+ #0").find(spec[pos]) != std::string_view::npos) ++pos;
    for (int field = 0; field != 2; ++field) {
        unsigned width = 0;
        while (pos < spec.size() && spec[pos] >= '0' && spec[pos] <= '9') {
            width = width * 10 + unsigned(spec[pos++] - '0');
            if (width > 8192) throw std::runtime_error("Resource format field too wide");
        }
        if (pos == spec.size() || spec[pos] != '.' || field) break;
        ++pos;
    }
    if (pos + 1 != spec.size()) throw std::runtime_error("Unsupported resource insert format");
    switch (spec[pos]) {
    case 's': return Kind::text;
    case 'd': case 'i': case 'c': return Kind::signedInt;
    case 'u': case 'x': case 'X': case 'o': return Kind::unsignedInt;
    case 'p': return Kind::pointer;
    default: throw std::runtime_error("Unsupported resource insert type");
    }
}
template<class T> std::string print(const std::string& spec, T value) {
    const int length = std::snprintf(nullptr, 0, spec.c_str(), value);
    if (length < 0 || std::size_t(length) > maxOutput) throw std::runtime_error("Resource insert too long");
    std::string result(std::size_t(length) + 1, '\0');
    std::snprintf(result.data(), result.size(), spec.c_str(), value);
    result.resize(length);
    return result;
}
}
// FormatMessage grammar reference:
// https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-formatmessagea
std::string format_message(std::string_view format, va_list* arguments) {
    std::vector<Token> tokens;
    std::array<Kind, 100> kinds{};
    unsigned count = 0;
    std::string literal;
    for (std::size_t pos = 0; pos < format.size();) {
        char ch = format[pos++];
        if (ch != '%') { literal += ch; continue; }
        if (pos == format.size()) throw std::runtime_error("Unterminated resource escape");
        ch = format[pos++];
        if (ch == '0') break;
        if (ch < '1' || ch > '9') {
            if (ch == 'n') literal += "\r\n";
            else if (ch == 'r') literal += '\r';
            else if (ch == 't') literal += '\t';
            else if (ch == 'b') literal += ' ';
            else literal += ch;
            continue;
        }
        unsigned index = unsigned(ch - '0');
        if (pos < format.size() && format[pos] >= '0' && format[pos] <= '9') index = index * 10 + unsigned(format[pos++] - '0');
        std::string spec = "s";
        if (pos < format.size() && format[pos] == '!') {
            const auto end = format.find('!', ++pos);
            if (end == std::string_view::npos) throw std::runtime_error("Unterminated resource insert");
            spec = format.substr(pos, end - pos);
            pos = end + 1;
        }
        const auto type = kind(spec);
        if (kinds[index] != Kind::none && kinds[index] != type) throw std::runtime_error("Resource insert has conflicting types");
        kinds[index] = type;
        count = std::max(count, index);
        tokens.push_back({std::move(literal), 0, Kind::none});
        literal.clear();
        tokens.push_back({"%" + spec, index, type});
    }
    tokens.push_back({std::move(literal), 0, Kind::none});
    if (count && !arguments) throw std::runtime_error("Missing resource format arguments");
    for (unsigned i = 1; i <= count; ++i)
        if (kinds[i] == Kind::none) throw std::runtime_error("Cannot infer skipped resource argument type");
    std::array<Value, 100> values{};
    if (count) {
        va_list copy;
        va_copy(copy, *arguments);
        for (unsigned i = 1; i <= count; ++i) {
            switch (kinds[i]) {
            case Kind::text: values[i].text = va_arg(copy, const char*); break;
            case Kind::signedInt: values[i].signedInt = va_arg(copy, int); break;
            case Kind::unsignedInt: values[i].unsignedInt = va_arg(copy, unsigned); break;
            case Kind::pointer: values[i].pointer = va_arg(copy, void*); break;
            default: break;
            }
        }
        va_end(copy);
    }
    std::string result;
    for (const auto& token : tokens) {
        std::string text;
        const auto& value = values[token.index];
        switch (token.kind) {
        case Kind::none: text = token.text; break;
        case Kind::text: text = print(token.text, value.text ? value.text : ""); break;
        case Kind::signedInt: text = print(token.text, value.signedInt); break;
        case Kind::unsignedInt: text = print(token.text, value.unsignedInt); break;
        case Kind::pointer: text = print(token.text, value.pointer); break;
        }
        if (text.size() > maxOutput - result.size()) throw std::runtime_error("Formatted resource too long");
        result += text;
    }
    return result;
}
std::string format_resource(const ResourceModule& module, std::uint32_t id, va_list* arguments) {
    if (!module.resources) throw std::runtime_error("Resource catalogue is not installed");
    return format_message(module.resources->string(module.name, id, module.language), arguments);
}
}
