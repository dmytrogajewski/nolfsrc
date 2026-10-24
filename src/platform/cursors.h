#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>
namespace nolf::platform {
struct CursorBitmap {
    std::uint32_t width=0,height=0,hot_x=0,hot_y=0;
    // Top-down straight-alpha 0xAARRGGBB; packed mono data/mask use SDL's
    // documented black/white/transparent/invert truth table, independently
    // of native handles. An adapter can reject unsupported color XOR.
    std::vector<std::uint32_t> rgba;
    std::vector<std::uint8_t> mono_data,mono_mask;
    bool color_xor=false;
};
class UnsupportedCursor : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class CursorShape { public: virtual ~CursorShape()=default; };
class CursorProvider {
public:
    virtual ~CursorProvider()=default;
    virtual std::shared_ptr<const CursorShape> create(const CursorBitmap&)=0;
    virtual void select(std::shared_ptr<const CursorShape>)=0;
    virtual void visible(bool)=0;
    virtual bool available() const=0;
};
// Cursor providers and their owned shapes are created/released on the main
// thread. Their video-subsystem lease can survive the window that created them.
std::shared_ptr<CursorProvider> make_sdl_cursors();
}
