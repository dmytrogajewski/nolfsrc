#include "engine/client/world_lightmaps.h"
#include "engine/portable/lightmap.h"
#include "engine/portable/string.h"
#include "engine/server/world_state.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
struct Snapshot {
    uint32 frames[2]; int between,blend; LTBOOL shadow; bool active;
    LTVector position,color; float radius;
    explicit Snapshot(const LightAnim& animation) : frames{animation.m_iFrames[0],animation.m_iFrames[1]},shadow(animation.m_bShadowMap),
        position(animation.m_vLightPos),color(animation.m_vLightColor),radius(animation.m_fLightRadius) {
        if (!std::isfinite(animation.m_fPercentBetween) || animation.m_fPercentBetween<0 || animation.m_fPercentBetween>1 ||
            !std::isfinite(animation.m_fBlendPercent) || animation.m_fBlendPercent<0 || animation.m_fBlendPercent>1 ||
            (shadow && (!finite(position) || !finite(color) || !std::isfinite(radius) || radius<0)))
            throw std::invalid_argument("invalid light animation state");
        between=animation.m_fPercentBetween<1.f/32 ? -1 : (!shadow && animation.m_fPercentBetween>31.f/32) ? 65 : int(animation.m_fPercentBetween*64);
        blend=int(animation.m_fBlendPercent*64); active=animation.m_fBlendPercent>=.02f;
    }
    bool operator==(const Snapshot& other) const {
        return frames[0]==other.frames[0] && frames[1]==other.frames[1] && between==other.between && blend==other.blend && shadow==other.shadow && active==other.active &&
            (!shadow || (position.x==other.position.x && position.y==other.position.y && position.z==other.position.z &&
             color.x==other.color.x && color.y==other.color.y && color.z==other.color.z && radius==other.radius));
    }
};
std::span<const std::byte> bytes(const CLAFrameEntry& entry) { return {static_cast<const std::byte*>(entry.m_pData),entry.m_nSize}; }
// One lightmap texel's step across and up the tile, in world units.
std::pair<LTVector,LTVector> texel_steps(const LightmapTile& tile) {
    const auto right_length=double(tile.right.MagSqr()),up_length=double(tile.up.MagSqr());
    return {right_length>0 ? tile.right*float(tile.grid/right_length) : LTVector(0,0,0),up_length>0 ? tile.up*float(tile.grid/up_length) : LTVector(0,0,0)};
}
}
DynamicWorldLight DynamicWorldLight::from(LTVector position,float radius,std::array<uint8,3> color,bool solid,bool front_only) {
    DynamicWorldLight light; light.position=position; light.radius=radius; light.color=color; light.solid=solid; light.front_only=front_only;
    for (unsigned i=0;i<3;++i) light.add[i]=2.f*color[i]-255.f;
    return light;
}
// r_DoesLightTouchPoly: the light's sphere reaches the plane (on its front
// for a front-only light) and the polygon's bounds.
bool DynamicWorldLight::touches(LTVector normal,float distance,LTVector centre,float extent) const {
    const auto d=normal.Dot(position)-distance;
    if (d>=radius || d<=-radius || (front_only && d<0)) return false;
    const auto reach=radius+extent; return (centre-position).MagSqr()<reach*reach;
}
void DynamicWorldLight::apply(LTVector point,float saturate,std::array<int,3>& color) const {
    const auto squared=(point-position).MagSqr(),radius_squared=radius*radius;
    if (squared>=radius_squared) return;
    if (solid) { for (unsigned i=0;i<3;++i) color[i]=this->color[i]; return; }
    // r_InitSaturateTable: ramp[i] = min(1, i/63 * LightSaturate).
    const auto ramp=std::min(1.f,float(int((1-squared/radius_squared)*63))*saturate/63);
    for (unsigned i=0;i<3;++i) color[i]+=int(add[i]*ramp);
}
struct WorldLightmaps::State {
    struct Page { Image pixels; std::uint64_t revision=0; explicit Page(uint32 side) : pixels(side,side,0xff000000) {} };
    std::shared_ptr<const CWorldData> source;
    const std::uint64_t generation;
    const std::thread::id owner=std::this_thread::get_id();
    const uint32 side;
    const std::size_t budget;
    std::size_t allocated=0;
    std::vector<LightmapTile> tiles;
    std::vector<LightmapLocation> locations;
    std::unordered_map<uint32,std::size_t> lookup;
    std::vector<Page> pages;
    std::vector<Snapshot> snapshots;
    std::vector<std::vector<std::size_t>> dependents;
    std::vector<LTVector> centres; std::vector<float> extents;
    std::vector<bool> lit;
    bool initialized=false;
    State(std::shared_ptr<const CWorldData> data,std::vector<LightmapTile> geometry,uint32 size,std::size_t limit)
        : source(std::move(data)),generation(source ? source->lighting_generation() : 0),side(size),budget(limit),tiles(std::move(geometry)) {
        if (!source || side<4 || side>Image::maximum_dimension || std::uint64_t(side)*side>Image::maximum_pixels || !budget || tiles.size()>1024*1024)
            throw std::invalid_argument("invalid lightmap atlas services or dimensions");
        charge(tiles.capacity()*sizeof(LightmapTile)+tiles.size()*(sizeof(LightmapLocation)+64));
        charge(source->m_Anims.GetSize()*(sizeof(Snapshot)+sizeof(std::vector<std::size_t>)));
        locations.reserve(tiles.size()); lookup.reserve(tiles.size()); dependents.resize(source->m_Anims.GetSize());
        uint32 x=0,y=0,row=0;
        for (std::size_t index=0;index<tiles.size();++index) {
            const auto& tile=tiles[index]; const auto width=uint32(tile.width),height=uint32(tile.height);
            if (!width || !height || std::uint64_t(width)*height>1024 || width+2>side || height+2>side ||
                !finite(tile.origin) || !finite(tile.right) || !finite(tile.up) || !std::isfinite(tile.grid) || tile.grid<=0 || tile.layers.size()>65535)
                throw std::invalid_argument("invalid lightmap tile geometry");
            charge(tile.layers.capacity()*sizeof(WorldLightmapReference)+tile.layers.size()*sizeof(std::size_t)*2);
            if (!lookup.emplace(tile.key,index).second) throw std::invalid_argument("duplicate lightmap polygon key");
            for (const auto& reference : tile.layers) {
                if (reference.animation>=dependents.size() || reference.polygon>=source->m_Anims[reference.animation].m_nLights)
                    throw std::invalid_argument("invalid polygon lightmap reference");
                dependents[reference.animation].push_back(index);
            }
            if (x+width+2>side) { x=0; y+=row; row=0; }
            if (pages.empty() || y+height+2>side) { charge(std::size_t(side)*side*4+sizeof(Page)); pages.emplace_back(side); x=y=row=0; }
            locations.push_back({pages.size()-1,x+1,y+1,width,height}); x+=width+2; row=std::max(row,height+2);
            const auto [right,up]=texel_steps(tile);
            centres.push_back(tile.origin+right*(float(width)/2)+up*(float(height)/2));
            extents.push_back(.5f*std::sqrt((right*float(width)).MagSqr()+(up*float(height)).MagSqr()));
        }
        lit.assign(tiles.size(),false);
    }
    void charge(std::size_t bytes) { if (bytes>budget || allocated>budget-bytes) throw std::bad_alloc(); allocated+=bytes; }
    void thread() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("world lightmaps used from another thread"); }
    void live() const { thread(); if (source->lighting_generation()!=generation) throw std::logic_error("lightmap source storage was replaced"); }
    std::vector<uint16> layer(const LightmapTile& tile,const LightAnim& animation,uint32 index) const {
        const auto count=std::size_t(tile.width)*tile.height;
        const auto& first=animation.m_ppFrames[animation.m_iFrames[0]][index];
        const auto& second=animation.m_ppFrames[animation.m_iFrames[1]][index];
        if (!animation.m_bShadowMap) {
            if (animation.m_fPercentBetween>31.f/32) return decode_lightmap555(bytes(second),count);
            auto output=decode_lightmap555(bytes(first),count);
            if (animation.m_iFrames[0]!=animation.m_iFrames[1] && animation.m_fPercentBetween>=1.f/32) {
                const auto other=decode_lightmap555(bytes(second),count);
                for (std::size_t i=0;i<count;++i) output[i]=interpolate_lightmap555(output[i],other[i],animation.m_fPercentBetween);
            }
            return output;
        }
        auto mask=decode_shadow_map(bytes(first),count);
        if (animation.m_iFrames[0]!=animation.m_iFrames[1] && animation.m_fPercentBetween>=1.f/32) {
            const auto other=decode_shadow_map(bytes(second),count); const auto fraction=int(animation.m_fPercentBetween*64);
            for (std::size_t i=0;i<count;++i) mask[i]=static_cast<uint8>(int(mask[i])+((int(other[i])-mask[i])*fraction>>6));
        }
        std::vector<uint16> output(count);
        double right_length=0,up_length=0;
        for (unsigned axis=0;axis<3;++axis) { right_length+=double(tile.right[axis])*tile.right[axis]; up_length+=double(tile.up[axis])*tile.up[axis]; }
        std::array<double,3> right{},up{};
        for (unsigned axis=0;axis<3;++axis) {
            if (right_length>0) right[axis]=double(tile.right[axis])*tile.grid/right_length;
            if (up_length>0) up[axis]=double(tile.up[axis])*tile.grid/up_length;
        }
        const double radius=animation.m_fLightRadius,radius_squared=radius*radius;
        for (uint32 y=0;y<tile.height;++y) for (uint32 x=0;x<tile.width;++x) {
            double squared=0;
            for (unsigned axis=0;axis<3;++axis) { const double delta=double(tile.origin[axis])+right[axis]*x+up[axis]*y-animation.m_vLightPos[axis]; squared+=delta*delta; }
            const auto pixel=std::size_t(y)*tile.width+x;
            if (!mask[pixel] || radius_squared<=0 || squared>=radius_squared) continue;
            const auto falloff=double(mask[pixel])/255*(1-squared/radius_squared);
            uint16 color=0;
            for (unsigned axis=0;axis<3;++axis) {
                const auto component=static_cast<unsigned>(std::clamp((2*double(animation.m_vLightColor[axis])-255)*falloff,0.,255.))>>3;
                color|=static_cast<uint16>(component<<((2-axis)*5));
            }
            output[pixel]=color;
        }
        return output;
    }
    Image compose(const LightmapTile& tile,std::span<const DynamicWorldLight* const> lights={},float saturate=1) const {
        std::vector<uint16> output(std::size_t(tile.width)*tile.height,0); bool first=true;
        for (const auto& reference : tile.layers) {
            const auto& animation=source->m_Anims[reference.animation];
            if (animation.m_iFrames[0]>=animation.m_nFrames || animation.m_iFrames[1]>=animation.m_nFrames || animation.m_fBlendPercent<.02f) continue;
            const auto values=layer(tile,animation,reference.polygon);
            if (first) output=values;
            else for (std::size_t i=0;i<output.size();++i) output[i]=add_lightmap555(output[i],values[i],animation.m_fBlendPercent);
            first=false;
        }
        // r_LightData: each texel with the lights added at its world position.
        if (!lights.empty()) {
            const auto [right,up]=texel_steps(tile);
            for (uint32 y=0;y<tile.height;++y) for (uint32 x=0;x<tile.width;++x) {
                auto& texel=output[std::size_t(y)*tile.width+x];
                std::array<int,3> color{((texel>>10)&31)<<3,((texel>>5)&31)<<3,(texel&31)<<3};
                const auto point=tile.origin+right*float(x)+up*float(y);
                for (const auto* light : lights) light->apply(point,saturate,color);
                for (auto& c : color) c=c<0 ? 0 : c>255 ? 31 : c>>3;
                texel=static_cast<uint16>(color[0]<<10|color[1]<<5|color[2]);
            }
        }
        return lightmap_image(tile.width,tile.height,output);
    }
    std::size_t update(std::span<const DynamicWorldLight> lights,float saturate) {
        live(); std::vector<Snapshot> next; next.reserve(source->m_Anims.GetSize());
        std::vector<bool> dirty(tiles.size(),!initialized),changed(pages.size(),false);
        // The lights reaching each tile; tiles lit last frame are composed
        // again whether or not they still are.
        std::vector<std::vector<const DynamicWorldLight*>> reaching(lights.empty() ? 0 : tiles.size());
        std::vector<bool> now(tiles.size(),false);
        if (!lights.empty()) for (std::size_t i=0;i<tiles.size();++i) for (const auto& light : lights)
            if (light.touches(tiles[i].normal,tiles[i].distance,centres[i],extents[i])) { reaching[i].push_back(&light); now[i]=true; }
        for (std::size_t i=0;i<tiles.size();++i) if (lit[i] || now[i]) dirty[i]=true;
        for (uint32 i=0;i<source->m_Anims.GetSize();++i) {
            next.emplace_back(source->m_Anims[i]);
            if (!initialized || !(next.back()==snapshots[i])) for (auto tile : dependents[i]) dirty[tile]=true;
        }
        struct Pending { std::size_t index; Image pixels; };
        std::vector<Pending> pending; const auto count=std::count(dirty.begin(),dirty.end(),true);
        std::size_t temporary=std::size_t(count)*sizeof(Pending)+next.capacity()*sizeof(Snapshot)+dirty.size()+changed.size();
        if (temporary>budget-allocated) throw std::bad_alloc();
        pending.reserve(count);
        for (std::size_t i=0;i<tiles.size();++i) if (dirty[i]) {
            const auto pixels=std::size_t(tiles[i].width)*tiles[i].height;
            if (pixels*12>budget-allocated-temporary) throw std::bad_alloc();
            pending.push_back({i,now[i] ? compose(tiles[i],reaching[i],saturate) : compose(tiles[i])}); temporary+=pixels*4;
        }
        // Pixel allocation/decoding completes before any atlas or snapshot is
        // changed, so a failed update can be retried without partial publication.
        for (const auto& update : pending) {
            const auto& location=locations[update.index]; auto& page=pages[location.page]; const auto pixels=update.pixels.pixels();
            for (int y=-1;y<=int(location.height);++y) for (int x=-1;x<=int(location.width);++x) {
                const auto sx=std::clamp(x,0,int(location.width)-1),sy=std::clamp(y,0,int(location.height)-1);
                const auto color=pixels[std::size_t(sy)*location.width+sx];
                auto& destination=page.pixels.pixels()[std::size_t(int(location.y)+y)*side+int(location.x)+x];
                if (destination!=color) { destination=color; changed[location.page]=true; }
            }
        }
        for (std::size_t i=0;i<pages.size();++i) if (changed[i] || !initialized) ++pages[i].revision;
        snapshots.swap(next); lit.swap(now); initialized=true; return pending.size();
    }
};
WorldLightmaps::WorldLightmaps(std::shared_ptr<const CWorldData> data,std::vector<LightmapTile> tiles,uint32 side,std::size_t budget)
    : state_(std::make_unique<State>(std::move(data),std::move(tiles),side,budget)) { state_->update({},1); }
WorldLightmaps::~WorldLightmaps()=default;
std::size_t WorldLightmaps::update(std::span<const DynamicWorldLight> lights,float saturate) {
    if (!std::isfinite(saturate) || saturate<0) throw std::invalid_argument("invalid light saturation");
    return state_->update(lights,saturate);
}
std::size_t WorldLightmaps::pages() const { state_->thread(); return state_->pages.size(); }
const Image& WorldLightmaps::image(std::size_t page) const { state_->thread(); return state_->pages.at(page).pixels; }
std::uint64_t WorldLightmaps::revision(std::size_t page) const { state_->thread(); return state_->pages.at(page).revision; }
std::optional<LightmapLocation> WorldLightmaps::find(uint32 key) const {
    state_->thread(); const auto it=state_->lookup.find(key); return it==state_->lookup.end() ? std::nullopt : std::optional(state_->locations[it->second]);
}
std::array<float,2> WorldLightmaps::coordinates(uint32 key,LTVector point) const {
    state_->thread(); if (!finite(point)) throw std::invalid_argument("nonfinite lightmap point");
    const auto it=state_->lookup.find(key); if (it==state_->lookup.end()) throw std::invalid_argument("unknown lightmap polygon");
    const auto& tile=state_->tiles[it->second]; const auto& location=state_->locations[it->second]; double u=0,v=0;
    for (unsigned axis=0;axis<3;++axis) { const double delta=double(point[axis])-tile.origin[axis]; u+=delta*tile.right[axis]; v+=delta*tile.up[axis]; }
    u=std::clamp(u/tile.grid,0.,double(tile.width-1)); v=std::clamp(v/tile.grid,0.,double(tile.height-1));
    return {float((location.x+u+.5)/state_->side),float((location.y+v+.5)/state_->side)};
}
std::unique_ptr<WorldLightmaps> world_lightmaps(std::shared_ptr<const CServerWorld> world,uint32 side,std::size_t budget) {
    if (!world || !world->Geometry()) throw std::invalid_argument("world lightmaps need loaded geometry");
    std::vector<LightmapTile> tiles; std::unordered_set<uint32> seen;
    for (const auto& animation : std::span(world->m_Anims.GetArray(),world->m_Anims.GetSize())) {
        if (ascii_compare(animation.m_Name,"LightAnim_BASE")!=0) continue;
        tiles.reserve(animation.m_nLights);
        for (const auto& key : std::span(animation.m_pLights,animation.m_nLights)) {
            if (key.m_n00>=world->m_Worlds.GetSize()) throw std::invalid_argument("lightmap model key outside world");
            const auto* model=world->m_Worlds[key.m_n00]->m_pWorldBsp;
            if (key.m_n02>=model->m_nParts) throw std::invalid_argument("lightmap polygon key outside model");
            const auto& polygon=*model->m_ppParts[key.m_n02]; const uint32 id=uint32(key.m_n00)<<16|key.m_n02;
            if (!(polygon.m_pSurface->m_nFlags&0x80) || !seen.insert(id).second) continue;
            LightmapTile tile; tile.key=id; tile.width=polygon.m_n48; tile.height=polygon.m_n49; tile.origin=polygon.m_v34;
            tile.grid=world->Geometry()->lightmap_grid_size(); polygon.m_pSurface->GetBasisVectors(&tile.right,&tile.up);
            if (const auto* plane=polygon.m_pSurface->m_pPlane) { tile.normal=plane->m_Normal; tile.distance=plane->m_Dist; }
            tile.layers.assign(polygon.lightmap_references().begin(),polygon.lightmap_references().end()); tiles.push_back(std::move(tile));
        }
        break;
    }
    return std::make_unique<WorldLightmaps>(std::move(world),std::move(tiles),side,budget);
}
}
