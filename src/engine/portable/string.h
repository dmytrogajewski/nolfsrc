#pragma once
#include <string_view>
namespace nolf {
inline char* ascii_uppercase(char* text) {
    if (text) for (auto* cursor = text; *cursor; ++cursor)
        if (*cursor >= 'a' && *cursor <= 'z') *cursor = char(*cursor - ('a' - 'A'));
    return text;
}
inline unsigned char ascii_lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
inline void ascii_lowercase(char* text) {
    if (text) for (; *text; ++text) *text = static_cast<char>(ascii_lower(static_cast<unsigned char>(*text)));
}
inline int ascii_compare(std::string_view left, std::string_view right) {
    const auto length = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t i = 0; i < length; ++i) {
        const auto a = ascii_lower(static_cast<unsigned char>(left[i]));
        const auto b = ascii_lower(static_cast<unsigned char>(right[i]));
        if (a != b) return a < b ? -1 : 1;
    }
    return left.size() == right.size() ? 0 : (left.size() < right.size() ? -1 : 1);
}
inline int ascii_compare(const char* left, const char* right) {
    return ascii_compare(std::string_view(left), std::string_view(right));
}
inline int ascii_compare_n(const char* left, const char* right, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto a = ascii_lower(static_cast<unsigned char>(left[i]));
        const auto b = ascii_lower(static_cast<unsigned char>(right[i]));
        if (a != b) return a < b ? -1 : 1;
        if (!a) break;
    }
    return 0;
}
}
