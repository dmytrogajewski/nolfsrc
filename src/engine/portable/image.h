#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nolf {
// Top-down, tightly packed 0xAARRGGBB pixels. Serialized formats are decoded
// explicitly; no native struct is overlaid on file data.
class Image {
public:
    static constexpr std::uint32_t maximum_dimension=16384;
    static constexpr std::size_t maximum_pixels=16*1024*1024;
    Image(std::uint32_t width,std::uint32_t height,std::uint32_t color=0xff000000);
    Image(const Image&)=default;
    Image& operator=(const Image&);
    Image(Image&&) noexcept;
    Image& operator=(Image&&) noexcept;
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    std::span<const std::uint32_t> pixels() const { return pixels_; }
    std::span<std::uint32_t> pixels() { return pixels_; }
    std::uint32_t at(std::uint32_t x,std::uint32_t y) const;
private:
    std::uint32_t width_,height_;
    std::vector<std::uint32_t> pixels_;
};
// ZSoft PCX, 8-bit indexed or three 8-bit RGB planes, raw or RLE encoded.
Image decode_pcx(std::span<const std::byte>);
// Uncompressed 24-bit BMP for native screenshots.
std::vector<std::byte> encode_bmp(const Image&);
}
