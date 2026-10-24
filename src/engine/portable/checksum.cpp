#include "engine/portable/checksum.h"
#include <array>

namespace nolf {
void Crc32::update(std::span<const std::byte> bytes) {
    static constexpr auto table = [] {
        std::array<std::uint32_t,256> result{};
        for (unsigned i=0; i<result.size(); ++i) {
            auto value = std::uint32_t(i);
            for (unsigned bit=0; bit<8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
            result[i] = value;
        }
        return result;
    }();
    for (auto byte : bytes) state_ = (state_ >> 8) ^ table[(state_ ^ std::to_integer<unsigned>(byte)) & 255];
}
}
