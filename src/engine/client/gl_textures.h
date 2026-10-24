#pragma once
#include "engine/client/client_textures.h"
#include <optional>
#include <span>

namespace nolf {
struct TextureSampling {
    bool alpha=true,mipmaps=true,linear=true,trilinear=true,repeat_s=true,repeat_t=true;
    float anisotropy=1;
    std::optional<std::uint32_t> color_key;
};
// The renderer's texture filtering (Bilinear, Trilinear, Anisotropic): what
// every draw's own sampling is limited to, or raised to for anisotropy.
struct TextureFilter { bool bilinear=true,trilinear=false; float anisotropy=1; };
struct TextureBinding { uint32 unit=0; std::shared_ptr<const TextureResource> resource; TextureSampling sampling; };
// GPU residency only. File decoding and SDK editing belong to ClientTextures;
// context creation and GL entry points belong to the render device adapter.
class GlTextures {
public:
    explicit GlTextures(std::size_t budget=256*1024*1024);
    ~GlTextures();
    GlTextures(const GlTextures&)=delete;
    GlTextures& operator=(const GlTextures&)=delete;
    void bind(const std::shared_ptr<const TextureResource>&,TextureSampling = {});
    void bind(const std::shared_ptr<const TextureResource>&,const std::shared_ptr<const TexturePixels>&,TextureSampling);
    // Multi-texture draws need independent GPU sampler/image state for each
    // unit, including when two units reference the same SDK texture identity.
    void bind_unit(uint32,const std::shared_ptr<const TextureResource>&,TextureSampling = {});
    // Preflight the complete draw's residency before binding either unit, so
    // LRU eviction cannot remove an earlier unit needed by this same draw.
    void bind_units(std::span<const TextureBinding>);
    void filter(TextureFilter);
    void collect();
    void clear();
    std::size_t bytes() const;
    std::size_t size() const;
private:
    void bind_impl(const std::shared_ptr<const TextureResource>&,const std::shared_ptr<const TexturePixels>&,TextureSampling,uint32);
    struct State;
    std::unique_ptr<State> state_;
};
}
