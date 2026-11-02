#pragma once
#include "engine/portable/assets.h"
#include "engine/portable/texture.h"
#include "iltclient.h"
#include "ilttexmod.h"
#include <thread>

namespace nolf {
struct TexturePixels {
    Texture texture;
    std::vector<std::uint8_t> bumpmap;
    // Some top-level texel is fully clear: DTXUtil's ColorKey cut it out.
    // Otherwise alpha is a gloss or envmap mask and the surface is solid.
    bool keyed=false;
};
// Renderer and SDK share this identity, never a reconstructed SharedTexture
// layout. A snapshot remains valid across edits, release and cache retirement.
class TextureResource {
public:
    const std::string& name() const { return name_; }
    std::uint64_t identity() const { return identity_; }
    std::uint64_t revision() const;
    std::shared_ptr<const TexturePixels> snapshot() const;
private:
    friend class ClientTextures;
    const std::thread::id owner_=std::this_thread::get_id();
    std::string name_;
    std::uint64_t identity_=0,service_=0,revision_=1;
    std::shared_ptr<const TexturePixels> original_,pixels_;
};
class ClientTextures final : public ILTTexMod {
    struct State;
public:
    explicit ClientTextures(std::shared_ptr<const Assets>,std::size_t budget=256*1024*1024);
    ~ClientTextures();
    ClientTextures(const ClientTextures&)=delete;
    ClientTextures& operator=(const ClientTextures&)=delete;
    std::shared_ptr<const TextureResource> load(std::string_view);
    void free_unused();
    std::size_t bytes() const;
    // One transaction per SDK Render call. An interrupted/failed call leaves
    // the preceding completed render's visibility intact.
    class Render {
    public:
        ~Render();
        Render(const Render&)=delete;
        Render& operator=(const Render&)=delete;
        void drawn(const std::shared_ptr<const TextureResource>&);
        void finish();
        // Unique process identity for one active render transaction.
        std::uint64_t token() const;
    private:
        friend class ClientTextures;
        explicit Render(std::shared_ptr<State>);
        std::shared_ptr<State> state_;
        std::uint64_t identity_=0;
    };
    Render begin_render();
    LTRESULT GetTextureHandle(char*,HTEXTURE&,uint32) override;
    LTRESULT ReleaseTextureHandle(HTEXTURE) override;
    LTRESULT GetTextureInfo(HTEXTURE,TextureInfo&) override;
    LTRESULT WasTextureDrawnLastFrame(HTEXTURE) override;
    LTRESULT LockTexture(HTEXTURE,const LTRect*,uint32,uint8*&,long&) override;
    LTRESULT UnlockTexture(HTEXTURE) override;
private:
    std::shared_ptr<State> state_;
};
}
