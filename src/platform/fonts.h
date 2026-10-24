#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nolf::platform {
struct FontRequest {
    std::string family="sans-serif";
    int width=0,height=16;
    bool italic=false,bold=false,underline=false;
};
struct FontLocation { std::filesystem::path file; std::string family; int index=0; };
struct FontMetrics { int height=0,ascent=0,descent=0,underline_y=0,underline_thickness=1; };
struct FontGlyph {
    std::uint32_t index=0;
    int width=0,height=0,left=0,top=0,advance=0;
    std::vector<std::uint8_t> coverage;
};
class FontLocator {
public:
    virtual ~FontLocator()=default;
    virtual FontLocation find(const FontRequest&)=0;
};
class FontFace {
public:
    virtual ~FontFace()=default;
    virtual const std::string& family() const=0;
    virtual FontMetrics metrics() const=0;
    virtual FontGlyph glyph(std::uint32_t codepoint,bool rasterize=true)=0;
    virtual int kerning(std::uint32_t left_glyph,std::uint32_t right_glyph)=0;
};
class FontProvider {
public:
    virtual ~FontProvider()=default;
    virtual std::shared_ptr<FontFace> load(const FontRequest&)=0;
};
// Discovery owns host font configuration. Rasterization owns the font bytes
// and third-party face/library lifetimes; game code sees neither API.
std::shared_ptr<FontLocator> system_font_locator();
std::shared_ptr<FontProvider> make_freetype_fonts(std::shared_ptr<FontLocator>);
}
