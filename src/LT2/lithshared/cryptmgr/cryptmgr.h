#pragma once
#include <array>
#include <cstdint>
#include <iosfwd>

class CCryptMgr {
public:
    CCryptMgr() = default;
    explicit CCryptMgr(const char* key) { SetKey(key); }
    void SetKey(const char* key);
    void Encrypt(std::istream& input, std::ostream& output);
    void Decrypt(std::istream& input, std::ostream& output);
private:
    // Retail passed sizeof(char*) on Win32. Preserve those four key bytes
    // explicitly, independently of the host pointer size.
    std::array<std::uint8_t, 4> key_{};
};
