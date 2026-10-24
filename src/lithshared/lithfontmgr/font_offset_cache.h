#pragma once
#include "lithfontdefs.h"
#include <filesystem>
#include <optional>
#include <string>

class ILTClient;
namespace nolf {
struct FontOffsetKey {
    std::string asset;
    std::uint32_t width, height, groups, pitch, crc32;
    bool operator==(const FontOffsetKey&) const = default;
};
std::optional<FontOffsetKey> font_offset_key(ILTClient&, const char* asset,
    std::uint32_t width, std::uint32_t height, std::uint32_t groups, std::uint32_t pitch);
class FontOffsetCache {
public:
    FontOffsetCache();
    explicit FontOffsetCache(std::filesystem::path);
    bool load(const FontOffsetKey&, LITHFONTOFFSETDATA&) const noexcept;
    bool store(const FontOffsetKey&, const LITHFONTOFFSETDATA&) const noexcept;
private:
    std::filesystem::path path_;
};
}
