#include "engine/client/gl_textures.h"
#include "rdev.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_map>

namespace nolf {
static_assert(std::endian::native==std::endian::little,"device BGRA upload requires little-endian pixels");
struct GlTextures::State {
    struct Key { std::uint64_t resource; uint32 unit; bool operator==(const Key&) const=default; };
    struct Hash { std::size_t operator()(const Key& key) const { return std::hash<std::uint64_t>{}(key.resource)^(std::size_t(key.unit)*0x9e3779b9u); } };
    struct Entry {
        std::weak_ptr<const TextureResource> resource;
        std::weak_ptr<const TexturePixels> pixels;
        std::uint64_t used=0;
        std::uint32_t name=0;
        std::size_t bytes=0;
        bool alpha=true;
        std::optional<std::uint32_t> color_key;
    };
    const std::thread::id owner=std::this_thread::get_id();
    const std::size_t budget;
    std::size_t bytes=0;
    std::uint64_t generation=0,clock=0;
    std::unordered_map<Key,Entry,Hash> entries;
    explicit State(std::size_t value) : budget(value) { if (!budget) throw std::invalid_argument("empty GPU texture budget"); }
    void require_owner() const {
        if (owner!=std::this_thread::get_id()) throw std::logic_error("GPU texture cache used from another thread");
    }
    void context() {
        require_owner(); const auto active=rd_DeviceGeneration();
        if (active!=generation) { entries.clear(); bytes=0; generation=active; }
        if (!active) throw std::logic_error("GPU texture cache requires an active device");
    }
    void erase(decltype(entries)::iterator entry) {
        if (generation && generation==rd_DeviceGeneration()) rd_DeleteTexture(entry->second.name);
        bytes-=entry->second.bytes; entries.erase(entry);
    }
    void clear() {
        if (owner==std::this_thread::get_id() && generation && generation==rd_DeviceGeneration())
            for (const auto& [id,entry] : entries) { (void)id; rd_DeleteTexture(entry.name); }
        entries.clear(); bytes=0;
    }
};
GlTextures::GlTextures(std::size_t budget) : state_(std::make_unique<State>(budget)) {}
GlTextures::~GlTextures() { state_->clear(); }
std::size_t GlTextures::bytes() const { state_->require_owner(); return state_->bytes; }
std::size_t GlTextures::size() const { state_->require_owner(); return state_->entries.size(); }
void GlTextures::clear() { state_->require_owner(); state_->clear(); }
void GlTextures::collect() {
    state_->context();
    for (auto it=state_->entries.begin();it!=state_->entries.end();) {
        if (it->second.resource.expired()) { auto dead=it++; state_->erase(dead); }
        else ++it;
    }
}
void GlTextures::bind(const std::shared_ptr<const TextureResource>& resource,TextureSampling sampling) {
    bind(resource,resource ? resource->snapshot() : nullptr,sampling);
}
void GlTextures::bind(const std::shared_ptr<const TextureResource>& resource,const std::shared_ptr<const TexturePixels>& snapshot,TextureSampling sampling) {
    bind_impl(resource,snapshot,sampling,0);
}
void GlTextures::bind_unit(uint32 unit,const std::shared_ptr<const TextureResource>& resource,TextureSampling sampling) {
    state_->context();
    if (unit>=static_cast<uint32>(g_RDCaps.nTextureUnits)) throw std::invalid_argument("texture unit exceeds device capacity");
    rd_ActiveTexture(static_cast<int>(unit)); bind_impl(resource,resource ? resource->snapshot() : nullptr,sampling,unit);
}
void GlTextures::bind_units(std::span<const TextureBinding> bindings) {
    state_->context(); std::vector<bool> seen(g_RDCaps.nTextureUnits); std::size_t size=0;
    if (bindings.size()>seen.size()) throw std::invalid_argument("too many texture bindings");
    for (const auto& binding : bindings) {
        if (!binding.resource || binding.unit>=seen.size() || seen[binding.unit] || !std::isfinite(binding.sampling.anisotropy) || binding.sampling.anisotropy<1 || (binding.sampling.color_key && *binding.sampling.color_key>0xffffff)) throw std::invalid_argument("invalid texture binding set");
        seen[binding.unit]=true;
        for (const auto& image : binding.resource->snapshot()->texture.mips) {
            const auto bytes=image.pixels().size_bytes(); if (bytes>state_->budget-size) throw std::length_error("draw textures exceed combined GPU budget"); size+=bytes;
            if (image.width()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize) || image.height()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize)) throw std::length_error("draw texture exceeds device dimensions");
        }
    }
    for (const auto& binding : bindings) bind_unit(binding.unit,binding.resource,binding.sampling);
}
void GlTextures::bind_impl(const std::shared_ptr<const TextureResource>& resource,const std::shared_ptr<const TexturePixels>& snapshot,TextureSampling sampling,uint32 unit) {
    auto& state=*state_; state.context();
    if (!resource || !snapshot || !std::isfinite(sampling.anisotropy) || sampling.anisotropy<1 || (sampling.color_key && *sampling.color_key>0xffffff)) throw std::invalid_argument("invalid GPU texture request");
    const auto& levels=snapshot->texture.mips;
    std::size_t size=0;
    for (const auto& image : levels) {
        if (image.width()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize) || image.height()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize))
            throw std::length_error("DTX exceeds device texture dimensions");
        // RGBA is a conservative residency budget even for opaque RGB storage.
        size+=image.pixels().size_bytes();
    }
    if (levels.empty() || size>state.budget) throw std::length_error("DTX exceeds GPU texture budget");
    const State::Key key{resource->identity(),unit}; auto found=state.entries.find(key);
    const auto old_size=found==state.entries.end() ? 0 : found->second.bytes;
    while (state.bytes-old_size>state.budget-size) {
        auto oldest=state.entries.end();
        for (auto it=state.entries.begin();it!=state.entries.end();++it) {
            if (it->first==key) continue;
            if (oldest==state.entries.end() || it->second.used<oldest->second.used) oldest=it;
        }
        if (oldest==state.entries.end()) throw std::length_error("GPU texture budget exhausted");
        state.erase(oldest);
    }
    auto [position,inserted]=state.entries.try_emplace(key);
    struct Rollback {
        State& state; decltype(state.entries)::iterator position; bool active;
        ~Rollback() { if (active) { rd_DeleteTexture(position->second.name); state.entries.erase(position); } }
    } rollback{state,position,inserted};
    auto& entry=position->second;
    if (!entry.name) entry.name=rd_GenTexture();
    rd_BindTexture(entry.name);
    const bool alpha=sampling.alpha || sampling.color_key.has_value();
    if (entry.pixels.lock()!=snapshot || entry.alpha!=sampling.alpha || entry.color_key!=sampling.color_key) {
        std::vector<std::uint32_t> keyed;
        for (std::size_t i=0;i<levels.size();++i) {
            const auto& image=levels[i];
            auto* pixels=image.pixels().data();
            if (sampling.color_key) {
                keyed.assign(image.pixels().begin(),image.pixels().end());
                for (auto& color : keyed) color=(color&0xffffff)==*sampling.color_key ? color&0xffffff : sampling.alpha ? color : color|0xff000000;
                pixels=keyed.data();
            }
            rd_TexImage(static_cast<int>(i),alpha ? RD_FMT_RGBA8 : RD_FMT_RGB8,image.width(),image.height(),RD_PIX_BGRA,pixels);
        }
        state.bytes=state.bytes-entry.bytes+size; entry.bytes=size;
        entry.resource=resource; entry.pixels=snapshot; entry.alpha=sampling.alpha;
        entry.color_key=sampling.color_key;
    }
    const bool mips=sampling.mipmaps && levels.size()>1;
    rd_TexMaxLevel(mips ? static_cast<int>(levels.size()-1) : 0);
    rd_TexFilter(sampling.linear ? (mips ? (sampling.trilinear ? RD_LINEAR_MIPMAP_LINEAR : RD_LINEAR_MIPMAP_NEAREST) : RD_LINEAR) : (mips ? RD_NEAREST_MIPMAP_NEAREST : RD_NEAREST),sampling.linear ? RD_LINEAR : RD_NEAREST);
    rd_TexWrap(sampling.repeat_s,sampling.repeat_t);
    if (g_RDCaps.bAniso) rd_TexAniso(std::min(sampling.anisotropy,g_RDCaps.fMaxAniso));
    entry.used=++state.clock; rollback.active=false;
}
}
