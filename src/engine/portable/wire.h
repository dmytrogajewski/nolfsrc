#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <type_traits>

namespace nolf::wire {
// Windows game packets and saves contain little-endian scalars, never native
// pointers or C++ object layouts. memcpy also permits unaligned packet cursors.
template<class T> T load(const void* data) {
    static_assert(std::is_arithmetic_v<T>);
    std::array<std::byte, sizeof(T)> bytes;
    std::memcpy(bytes.data(), data, bytes.size());
    if constexpr (std::endian::native == std::endian::big)
        std::reverse(bytes.begin(), bytes.end());
    return std::bit_cast<T>(bytes);
}
template<class T> void store(void* data, T value) {
    static_assert(std::is_arithmetic_v<T>);
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    if constexpr (std::endian::native == std::endian::big)
        std::reverse(bytes.begin(), bytes.end());
    std::memcpy(data, bytes.data(), bytes.size());
}
}
