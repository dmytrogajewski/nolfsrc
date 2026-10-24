#pragma once
#include <cstdint>
#include <span>

namespace nolf::attributes {
// Used for compatibility with original attribute files, not new cryptography.
class Blowfish {
public:
    explicit Blowfish(std::span<const std::uint8_t> key);
    void encipher(std::uint32_t& left, std::uint32_t& right) const;
    void decipher(std::uint32_t& left, std::uint32_t& right) const;
private:
    std::uint32_t function(std::uint32_t) const;
    std::uint32_t p_[18], s_[4][256];
};
}
