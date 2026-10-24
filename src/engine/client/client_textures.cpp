#include "engine/client/client_textures.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace nolf {
namespace {
std::atomic<std::uint64_t> next_identity{1};
std::uint64_t identity() {
    auto value=next_identity.load(std::memory_order_relaxed);
    do { if (value==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("texture identities exhausted"); }
    while (!next_identity.compare_exchange_weak(value,value+1,std::memory_order_relaxed));
    return value;
}
void require_thread(std::thread::id owner) {
    if (owner!=std::this_thread::get_id()) throw std::logic_error("client textures used from another thread");
}
struct Budget {
    std::atomic<std::size_t> bytes{0};
    const std::size_t limit;
    explicit Budget(std::size_t value) : limit(value) {}
};
struct Charge {
    std::shared_ptr<Budget> budget;
    std::size_t bytes;
    Charge(std::shared_ptr<Budget> value,std::size_t size) : budget(std::move(value)),bytes(size) {
        auto current=budget->bytes.load(std::memory_order_relaxed);
        do { if (bytes>budget->limit || current>budget->limit-bytes) throw std::bad_alloc(); }
        while (!budget->bytes.compare_exchange_weak(current,current+bytes,std::memory_order_relaxed));
    }
    ~Charge() { budget->bytes.fetch_sub(bytes,std::memory_order_relaxed); }
};
struct ChargeOwner {
    std::shared_ptr<Charge> charge;
};
// Bases are destroyed in reverse order, after all pixel storage is released.
struct Pixels : ChargeOwner,TexturePixels {};
std::size_t extent(const Texture& texture) {
    std::size_t bytes=sizeof(Pixels)+texture.header.commands.size();
    for (const auto& image : texture.mips) bytes+=sizeof(Image)+image.pixels().size_bytes();
    for (const auto& section : texture.sections) bytes+=sizeof(TextureSection)+section.type.size()+section.name.size()+section.data.size();
    return bytes;
}
template<class F> LTRESULT result(F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
Image downsample(const Image& source,unsigned width,unsigned height) {
    Image output(width,height);
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        std::uint32_t pixel=0;
        for (unsigned shift : {0u,8u,16u,24u}) {
            unsigned sum=0,count=0;
            for (unsigned dy=0;dy<2 && y*2+dy<source.height();++dy)
                for (unsigned dx=0;dx<2 && x*2+dx<source.width();++dx) { sum+=(source.at(x*2+dx,y*2+dy)>>shift)&255; ++count; }
            pixel|=(sum/count)<<shift;
        }
        output.pixels()[std::size_t(y)*width+x]=pixel;
    }
    return output;
}
}
std::uint64_t TextureResource::revision() const { require_thread(owner_); return revision_; }
std::shared_ptr<const TexturePixels> TextureResource::snapshot() const { require_thread(owner_); return pixels_; }
struct ClientTextures::State {
    struct Entry {
        std::shared_ptr<TextureResource> resource;
        std::shared_ptr<Charge> charge;
        Image edit;
        std::vector<std::uint8_t> bump;
        uint32 flags=0,references=1;
        bool locked=false;
        Entry(std::shared_ptr<TextureResource> texture,std::shared_ptr<Budget> budget,uint32 options)
            : resource(std::move(texture)),charge(std::make_shared<Charge>(std::move(budget),resource->original_->texture.mips[0].pixels().size_bytes()+(options&THANDLE_BUMPMAP ? resource->original_->texture.mips[0].pixels().size() : 0))),
              edit(resource->original_->texture.mips[0]),flags(options) {
            if (flags&THANDLE_BUMPMAP) {
                bump.resize(edit.pixels().size());
                for (std::size_t i=0;i<bump.size();++i) bump[i]=static_cast<std::uint8_t>(edit.pixels()[i]>>24);
            }
        }
    };
    const std::thread::id owner=std::this_thread::get_id();
    const std::uint64_t service=identity();
    std::shared_ptr<const Assets> assets;
    std::shared_ptr<Budget> budget;
    std::unordered_map<std::string,std::weak_ptr<TextureResource>> cache;
    std::unordered_map<HTEXTURE,std::unique_ptr<Entry>> handles;
    std::unordered_map<std::uint64_t,HTEXTURE> editing;
    std::unordered_set<std::uint64_t> drawn,last_drawn;
    bool rendering=false,retired=false;
    State(std::shared_ptr<const Assets> files,std::size_t limit) : assets(std::move(files)),budget(std::make_shared<Budget>(limit)) {
        if (!assets || !limit) throw std::invalid_argument("texture cache needs assets and a budget");
    }
    void require_owner() const {
        require_thread(owner); if (retired) throw std::logic_error("texture service retired");
    }
    Entry* find(HTEXTURE handle) {
        auto it=handles.find(handle); return it==handles.end() ? nullptr : it->second.get();
    }
    std::shared_ptr<TextureResource> load(std::string_view filename) {
        require_owner(); const auto name=platform::asset_key(filename);
        if (name.empty()) throw std::invalid_argument("empty texture filename");
        if (const auto found=cache.find(name);found!=cache.end()) if (auto resource=found->second.lock()) return resource;
        const auto source=assets->locate(name);
        if (source.size>256*1024*1024) throw std::invalid_argument("DTX exceeds file budget");
        std::vector<std::byte> bytes(static_cast<std::size_t>(source.size)); source.read_at(0,bytes);
        auto decoded=decode_dtx(bytes);
        auto charge=std::make_shared<Charge>(budget,extent(decoded));
        auto pixels=std::make_shared<Pixels>(); pixels->charge=std::move(charge); pixels->texture=std::move(decoded);
        auto resource=std::make_shared<TextureResource>(); resource->name_=name; resource->identity_=identity(); resource->service_=service;
        resource->original_=pixels; resource->pixels_=std::move(pixels);
        if (cache.size()>=65536) {
            std::erase_if(cache,[](const auto& entry) { return entry.second.expired(); });
            if (cache.size()>=65536) throw std::bad_alloc();
        }
        cache[name]=resource; return resource;
    }
    void restore(Entry& entry) {
        if (entry.resource->pixels_!=entry.resource->original_) {
            entry.resource->pixels_=entry.resource->original_; ++entry.resource->revision_;
        }
    }
    void retire() {
        retired=true;
        for (auto& [handle,entry] : handles) { (void)handle; restore(*entry); }
        handles.clear(); editing.clear(); drawn.clear(); last_drawn.clear(); rendering=false;
    }
};
ClientTextures::ClientTextures(std::shared_ptr<const Assets> assets,std::size_t budget) : state_(std::make_shared<State>(std::move(assets),budget)) {}
ClientTextures::~ClientTextures() { state_->retire(); }
std::shared_ptr<const TextureResource> ClientTextures::load(std::string_view name) { return state_->load(name); }
void ClientTextures::free_unused() { state_->require_owner(); std::erase_if(state_->cache,[](const auto& entry) { return entry.second.expired(); }); }
std::size_t ClientTextures::bytes() const { return state_->budget->bytes.load(std::memory_order_relaxed); }
ClientTextures::Render::Render(std::shared_ptr<State> state) : state_(std::move(state)) {
    state_->require_owner(); if (state_->rendering) throw std::logic_error("nested texture render transaction");
    identity_=identity();
    state_->drawn.clear(); state_->rendering=true;
}
ClientTextures::Render::~Render() { if (state_) { state_->drawn.clear(); state_->rendering=false; } }
std::uint64_t ClientTextures::Render::token() const {
    if (!state_) throw std::logic_error("texture render transaction already completed");
    state_->require_owner(); return identity_;
}
void ClientTextures::Render::drawn(const std::shared_ptr<const TextureResource>& texture) {
    if (!state_) throw std::logic_error("texture render transaction already completed");
    state_->require_owner();
    if (!texture || texture->service_!=state_->service) throw std::invalid_argument("foreign rendered texture");
    state_->drawn.insert(texture->identity());
}
void ClientTextures::Render::finish() {
    if (!state_) throw std::logic_error("texture render transaction already completed");
    state_->require_owner(); state_->last_drawn.swap(state_->drawn); state_->drawn.clear(); state_->rendering=false; state_.reset();
}
ClientTextures::Render ClientTextures::begin_render() { return Render(state_); }
LTRESULT ClientTextures::GetTextureHandle(char* filename,HTEXTURE& output,uint32 flags) {
    output=nullptr; const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); if (!filename || !*filename || (flags&~THANDLE_BUMPMAP)) return LT_INVALIDPARAMS;
        if (!state->assets->contains(filename)) return LT_NOTFOUND;
        auto resource=state->load(filename);
        if (const auto found=state->editing.find(resource->identity());found!=state->editing.end()) {
            auto& entry=*state->find(found->second);
            if (entry.flags!=flags) return LT_INVALIDPARAMS;
            if (entry.references==std::numeric_limits<uint32>::max()) return LT_ERROR;
            ++entry.references; output=found->second; return LT_OK;
        }
        if (state->handles.size()>=65536) return LT_OUTOFMEMORY;
        auto entry=std::make_unique<State::Entry>(resource,state->budget,flags);
        const auto handle=reinterpret_cast<HTEXTURE>(static_cast<std::uintptr_t>(identity()));
        state->handles.emplace(handle,std::move(entry));
        try { state->editing.emplace(resource->identity(),handle); }
        catch (...) { state->handles.erase(handle); throw; }
        output=handle; return LT_OK;
    });
}
LTRESULT ClientTextures::ReleaseTextureHandle(HTEXTURE handle) {
    const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); auto* entry=state->find(handle); if (!entry) return LT_INVALIDPARAMS;
        if (entry->references==1) {
            if (entry->locked) return LT_ERROR;
            state->restore(*entry); state->editing.erase(entry->resource->identity()); state->handles.erase(handle);
        } else --entry->references;
        return LT_OK;
    });
}
LTRESULT ClientTextures::GetTextureInfo(HTEXTURE handle,TextureInfo& info) {
    info.m_Width=info.m_Height=0; const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); const auto* entry=state->find(handle); if (!entry) return LT_INVALIDPARAMS;
        info.m_Width=entry->edit.width(); info.m_Height=entry->edit.height(); return LT_OK;
    });
}
LTRESULT ClientTextures::WasTextureDrawnLastFrame(HTEXTURE handle) {
    const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); const auto* entry=state->find(handle); if (!entry) return LT_INVALIDPARAMS;
        return state->last_drawn.contains(entry->resource->identity()) ? LT_YES : LT_NO;
    });
}
LTRESULT ClientTextures::LockTexture(HTEXTURE handle,const LTRect* rectangle,uint32 type,uint8*& data,long& pitch) {
    data=nullptr; pitch=0; const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); auto* entry=state->find(handle); if (!entry) return LT_INVALIDPARAMS;
        if (entry->locked) return LT_ERROR;
        if (type!=TLOCK_TEXTURE && type!=TLOCK_BUMPMAP) return LT_INVALIDPARAMS;
        const auto& image=entry->edit;
        const LTRect rect=rectangle ? *rectangle : LTRect{0,0,int(image.width()),int(image.height())};
        if (rect.left<0 || rect.top<0 || rect.right<=rect.left || rect.bottom<=rect.top ||
            rect.right>int(image.width()) || rect.bottom>int(image.height())) return LT_INVALIDPARAMS;
        const auto offset=std::size_t(rect.top)*image.width()+rect.left;
        if (type==TLOCK_TEXTURE) { data=reinterpret_cast<uint8*>(entry->edit.pixels().data()+offset); pitch=long(image.width()*4); }
        else {
            if (entry->bump.empty()) return LT_NOTINITIALIZED;
            data=entry->bump.data()+offset; pitch=long(image.width());
        }
        entry->locked=true; return LT_OK;
    });
}
LTRESULT ClientTextures::UnlockTexture(HTEXTURE handle) {
    const auto state=state_;
    return result([&]() -> LTRESULT {
        state->require_owner(); auto* entry=state->find(handle); if (!entry) return LT_INVALIDPARAMS;
        if (!entry->locked) return LT_NOTINITIALIZED;
        const auto& original=entry->resource->original_->texture;
        auto charge=std::make_shared<Charge>(state->budget,extent(original)+entry->bump.size());
        auto pixels=std::make_shared<Pixels>(); pixels->charge=std::move(charge);
        pixels->texture.header=original.header; pixels->texture.sections=original.sections; pixels->bumpmap=entry->bump;
        auto& levels=pixels->texture.mips; levels.reserve(original.mips.size()); levels.push_back(entry->edit);
        for (std::size_t i=1;i<original.mips.size();++i) levels.push_back(downsample(levels.back(),original.mips[i].width(),original.mips[i].height()));
        entry->resource->pixels_=std::move(pixels); ++entry->resource->revision_; entry->locked=false; return LT_OK;
    });
}
}
