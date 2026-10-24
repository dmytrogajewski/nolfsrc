#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace nolf {
// File/wire data never inherits the compiler's padding or pointer width.
class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> data) : data_(data) {}
    std::size_t remaining() const { return data_.size() - position_; }
    std::span<const std::byte> bytes(std::size_t length) {
        if (length > remaining()) throw std::runtime_error("truncated binary data");
        auto result = data_.subspan(position_, length);
        position_ += length;
        return result;
    }
    template<class T> T integer() {
        static_assert(std::is_unsigned_v<T>);
        auto input = bytes(sizeof(T));
        T result{};
        for (std::size_t i = 0; i < sizeof(T); ++i) result |= static_cast<T>(std::to_integer<unsigned char>(input[i])) << (8 * i);
        return result;
    }
    float real() { return std::bit_cast<float>(integer<std::uint32_t>()); }
    std::string cstring() {
        std::string result;
        while (true) {
            auto c = integer<std::uint8_t>();
            if (!c) return result;
            result += static_cast<char>(c);
        }
    }
private:
    std::span<const std::byte> data_;
    std::size_t position_{};
};

class BinaryWriter {
public:
    void reserve(std::size_t capacity) { data_.reserve(capacity); }
    template<class T> void integer(T value) {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i) data_.push_back(static_cast<std::byte>((value >> (8 * i)) & 255));
    }
    void real(float value) { integer(std::bit_cast<std::uint32_t>(value)); }
    void bytes(std::span<const std::byte> value) { data_.insert(data_.end(), value.begin(), value.end()); }
    const std::vector<std::byte>& data() const { return data_; }
private:
    std::vector<std::byte> data_;
};
}
