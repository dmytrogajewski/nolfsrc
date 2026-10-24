#include "cryptmgr.h"
#include "blowfish.h"
#include <array>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace {
std::uint32_t load_word(const char* bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
    return value;
}
void store_word(char* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<char>(value >> (i * 8));
}
void transform(std::array<char, 8>& block, const nolf::attributes::Blowfish& cipher, bool encrypt) {
    auto left = load_word(block.data()), right = load_word(block.data() + 4);
    if (encrypt) cipher.encipher(left, right);
    else cipher.decipher(left, right);
    store_word(block.data(), left); store_word(block.data() + 4, right);
}
}

void CCryptMgr::SetKey(const char* key) {
    key_.fill(0);
    if (key) for (unsigned i = 0; i < key_.size() && key[i]; ++i)
        key_[i] = static_cast<std::uint8_t>(key[i]);
}
void CCryptMgr::Encrypt(std::istream& input, std::ostream& output) {
    const nolf::attributes::Blowfish cipher(key_);
    for (;;) {
        std::array<char, 8> block{};
        input.read(block.data(), block.size());
        const auto length = input.gcount();
        if (input.bad() || (!input.eof() && input.fail())) throw std::runtime_error("Attribute encryption read failed");
        transform(block, cipher, true);
        output.write(block.data(), block.size());
        if (length != 8) {
            output.put(static_cast<char>(length));
            break;
        }
    }
    if (!output) throw std::runtime_error("Attribute encryption write failed");
}
void CCryptMgr::Decrypt(std::istream& input, std::ostream& output) {
    const nolf::attributes::Blowfish cipher(key_);
    std::array<char, 8> previous{};
    input.read(previous.data(), previous.size());
    if (input.gcount() != 8 || !input) throw std::runtime_error("Truncated encrypted attributes");
    transform(previous, cipher, false);
    for (;;) {
        std::array<char, 8> next{};
        input.read(next.data(), next.size());
        const auto length = input.gcount();
        if (input.bad()) throw std::runtime_error("Attribute decryption read failed");
        if (length == 1 && input.eof()) {
            const auto tail = static_cast<unsigned char>(next[0]);
            if (tail > 8) throw std::runtime_error("Invalid encrypted attribute tail");
            output.write(previous.data(), tail);
            break;
        }
        if (length != 8 || !input) throw std::runtime_error("Truncated encrypted attributes");
        output.write(previous.data(), previous.size());
        transform(next, cipher, false);
        previous = next;
    }
    if (!output) throw std::runtime_error("Attribute decryption write failed");
}
