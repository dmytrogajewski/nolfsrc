#pragma once
#include "engine/client/canvas_draw.h"
#include "engine/client/gl_textures.h"
namespace nolf {
class GlCanvas {
public:
    explicit GlCanvas(GlTextures& textures) : textures_(textures) {}
    std::size_t draw(std::span<const CanvasPrimitive>,ClientTextures::Render&);
private:
    GlTextures& textures_;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
