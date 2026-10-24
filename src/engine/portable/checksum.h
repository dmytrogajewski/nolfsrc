#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace nolf {
class Crc32 {
public:
    void update(std::span<const std::byte>);
    std::uint32_t value() const { return state_ ^ 0xffffffffu; }
private:
    std::uint32_t state_ = 0xffffffffu;
};
}
