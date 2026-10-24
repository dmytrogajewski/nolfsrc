#pragma once
#include <cstddef>
#include <cstdint>

namespace nolf::text {
// Native strings are UTF-8 on every host. Malformed input consumes one byte;
// traversal never crosses the terminator or treats it as another character.
inline const char* next(const char* value) {
    if (!value || !*value) return value;
    const auto first = static_cast<unsigned char>(*value);
    const unsigned size = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 1;
    std::uint32_t code = first & (size == 2 ? 31 : size == 3 ? 15 : size == 4 ? 7 : 127);
    for (unsigned i=1; i<size; ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        if ((byte & 0xc0) != 0x80) return value+1;
        code = (code << 6) | (byte & 63);
    }
    if ((size == 3 && code < 0x800) || (size == 4 && code < 0x10000) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return value+1;
    return value+size;
}
inline char* next(char* value) { return const_cast<char*>(next(static_cast<const char*>(value))); }
inline const char* advance(const char* value, std::size_t count) {
    while (count-- && value && *value) value = next(value);
    return value;
}
inline char* advance(char* value, std::size_t count) { return const_cast<char*>(advance(static_cast<const char*>(value),count)); }
inline std::size_t length(const char* value) {
    std::size_t count=0;
    while (value && *value) { value=next(value); ++count; }
    return count;
}
}
