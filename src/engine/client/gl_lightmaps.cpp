#include "engine/client/gl_lightmaps.h"
#include "rdev.h"
#include <bit>
#include <thread>

namespace nolf {
static_assert(std::endian::native==std::endian::little,"device BGRA upload requires little-endian pixels");
struct GlLightmaps::State {
    struct Page { uint32 name=0; std::uint64_t revision=0,used=0; std::size_t bytes=0; };
    const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<const WorldLightmaps> atlas;
    std::vector<Page> pages;
    const std::size_t budget;
    std::size_t bytes=0,count=0;
    std::uint64_t generation=0,clock=0;
    State(std::shared_ptr<const WorldLightmaps> source,std::size_t limit) : atlas(std::move(source)),budget(limit) {
        if (!atlas || !budget) throw std::invalid_argument("invalid GPU lightmap services or budget");
        pages.resize(atlas->pages());
    }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("GPU lightmaps used from another thread"); }
    void erase(Page& page) {
        if (page.name) {
            if (generation && generation==rd_DeviceGeneration()) rd_DeleteTexture(page.name);
            bytes-=page.bytes; --count; page={};
        }
    }
    void clear() { for (auto& page : pages) erase(page); }
    void context() {
        thread(); const auto current=rd_DeviceGeneration();
        if (generation!=current) { clear(); generation=current; }
        if (!current) throw std::logic_error("GPU lightmaps require an active device");
    }
};
GlLightmaps::GlLightmaps(std::shared_ptr<const WorldLightmaps> atlas,std::size_t budget)
    : state_(std::make_unique<State>(std::move(atlas),budget)) {}
GlLightmaps::~GlLightmaps() { if (state_->owner==std::this_thread::get_id()) state_->clear(); }
void GlLightmaps::clear() { state_->thread(); state_->clear(); }
std::size_t GlLightmaps::bytes() const { state_->thread(); return state_->bytes; }
std::size_t GlLightmaps::size() const { state_->thread(); return state_->count; }
void GlLightmaps::bind(std::size_t index,bool linear) {
    auto& state=*state_; state.context();
    auto& page=state.pages.at(index); const auto& image=state.atlas->image(index);
    const auto size=image.pixels().size_bytes();
    if (image.width()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize) || image.height()>static_cast<unsigned>(g_RDCaps.nMaxTextureSize) || size>state.budget)
        throw std::length_error("lightmap page exceeds device dimensions or residency budget");
    if (!page.name) {
        while (state.bytes>state.budget-size) {
            State::Page* oldest=nullptr;
            for (auto& other : state.pages) if (other.name && (!oldest || other.used<oldest->used)) oldest=&other;
            if (!oldest) throw std::length_error("GPU lightmap residency budget exhausted");
            state.erase(*oldest);
        }
        page.name=rd_GenTexture(); page.bytes=size; state.bytes+=size; ++state.count;
    }
    rd_BindTexture(page.name);
    const auto revision=state.atlas->revision(index);
    if (page.revision!=revision) {
        rd_TexImage(0,RD_FMT_RGB8,image.width(),image.height(),RD_PIX_BGRA,image.pixels().data());
        page.revision=revision;
    }
    rd_TexMaxLevel(0); rd_TexFilter(linear ? RD_LINEAR : RD_NEAREST,linear ? RD_LINEAR : RD_NEAREST); rd_TexWrap(false,false);
    page.used=++state.clock;
}
}
