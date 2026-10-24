#include "engine/client/client_effects.h"
#include "engine/server/objtypes.h"
#include "engine/server/object_manager.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace nolf {
namespace {
constexpr std::size_t budget=262144;
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::max({std::fabs(v.x),std::fabs(v.y),std::fabs(v.z)})<=1e12f; }
bool finite(const LTLine& line) {
    for (const auto& p : line.m_Points) if (!finite(p.m_Pos) || !std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b) || !std::isfinite(p.a)) return false;
    return true;
}
void valid(bool value) { if (!value) throw std::invalid_argument("invalid native effect request"); }
void sync(CObjParticleSystem& system) {
    system.m_p1c8=static_cast<CParticle*>(system.m_Head.m_pNext); system.m_p1e0=system.m_Head.m_pPrev;
}
void sync(CObjLineSystem& system) { system.m_pCursorB=system.m_Head.m_pNext; system.m_pCursorA=system.m_Head.m_pPrev; }
}
ClientEffects::ClientEffects(std::shared_ptr<const Assets> assets) : assets_(std::move(assets)),sprites_(assets_) { valid(bool(assets_)); }
void ClientEffects::require_owner() const { if (owner_!=std::this_thread::get_id()) throw std::logic_error("client effects used from another thread"); }
const ClientEffects::Entry& ClientEffects::get(HOBJECT object,uint8 type) const {
    require_owner(); const auto found=objects_.find(object);
    valid(found!=objects_.end() && found->second.guard.same_data() && object->m_ObjectType==type); return found->second;
}
ClientEffects::Entry& ClientEffects::get(HOBJECT object,uint8 type) { return const_cast<Entry&>(std::as_const(*this).get(object,type)); }
void ClientEffects::add_object(HOBJECT object) {
    require_owner();
    if (object->m_ObjectType!=OT_PARTICLESYSTEM && object->m_ObjectType!=OT_LINESYSTEM && object->m_ObjectType!=OT_POLYGRID) return;
    valid(objects_.try_emplace(object,object).second);
    if (object->m_ObjectType==OT_PARTICLESYSTEM) {
        auto& system=*static_cast<CObjParticleSystem*>(object); system.m_Head.m_pNext=system.m_Head.m_pPrev=&system.m_Head; sync(system);
    } else if (object->m_ObjectType==OT_LINESYSTEM) {
        auto& system=*static_cast<CObjLineSystem*>(object); system.m_Head.m_pNext=system.m_Head.m_pPrev=&system.m_Head; sync(system);
    }
}
void ClientEffects::forget(HOBJECT object) {
    require_owner(); const auto found=objects_.find(object); if (found==objects_.end()) return;
    for (auto line : found->second.lines) lines_.erase(line);
    if (object->m_ObjectType==OT_POLYGRID) grid_points_-=static_cast<CObjPolyGrid*>(object)->m_Heights.size();
    particle_count_-=found->second.particles.size(); objects_.erase(found);
}
void ClientEffects::clear() { require_owner(); objects_.clear(); lines_.clear(); particle_count_=grid_points_=0; }
LTRESULT ClientEffects::setup(HOBJECT object,const char* texture,float gravity,uint32 flags,float radius) {
    auto& entry=get(object,OT_PARTICLESYSTEM);
    valid(texture && std::isfinite(gravity) && std::fabs(gravity)<=1e12f && std::isfinite(radius) && radius>=0 && radius<=1e8f && !(flags&~uint32(PS_BOUNCE|PS_NEVERDIE|PS_DUMB)));
    auto name=*texture ? platform::asset_key(texture) : std::string{}; std::shared_ptr<SpriteResource> sprite;
    if (!name.empty()) {
        if (!assets_->contains(name)) return LT_NOTFOUND;
        if (name.ends_with(".spr")) sprite=sprites_.load(name);
        else if (!name.ends_with(".dtx")) return LT_INVALIDPARAMS;
    }
    entry.texture=std::move(name); entry.sprite=std::move(sprite); entry.time=0;
    auto& system=*static_cast<CObjParticleSystem*>(object); system.m_f248=gravity; system.m_f24c=radius; system.m_u258=flags; return LT_OK;
}
LTRESULT ClientEffects::software_color(HOBJECT object,float r,float g,float b) {
    get(object,OT_PARTICLESYSTEM); valid(finite({r,g,b})); auto& system=*static_cast<CObjParticleSystem*>(object);
    system.m_u1ec=uint8(std::clamp(r,0.f,1.f)*255); system.m_u1ed=uint8(std::clamp(g,0.f,1.f)*255); system.m_u1ee=uint8(std::clamp(b,0.f,1.f)*255); return LT_OK;
}
LTParticle* ClientEffects::add_particle(HOBJECT object,LTVector position,LTVector velocity,LTVector color,float lifetime) {
    auto& entry=get(object,OT_PARTICLESYSTEM); valid(finite(position) && finite(velocity) && finite(color) && std::isfinite(lifetime) && lifetime>=0 && lifetime<=1e12f);
    if (particle_count_>=budget) throw std::bad_alloc();
    auto& system=*static_cast<CObjParticleSystem*>(object); auto* bank=&system.m_pAttachMgr->m_Bank14; auto* storage=sb_Allocate(bank); if (!storage) throw std::bad_alloc();
    auto* particle=new(storage) CParticle;
    try { entry.particles.insert(particle); } catch (...) { particle->~CParticle(); sb_Free(bank,storage); throw; }
    particle->m_Pos=position; particle->m_Velocity=velocity; particle->m_Color=color; particle->m_Lifetime=particle->m_TotalLifetime=lifetime; particle->m_Size=system.m_f24c;
    particle->m_pNext=&system.m_Head; particle->m_pPrev=system.m_Head.m_pPrev;
    particle->m_pPrev->m_pNext=particle; system.m_Head.m_pPrev=particle; sync(system); ++system.m_u228; ++particle_count_; return particle;
}
float ClientEffects::random() { random_^=random_<<13; random_^=random_>>17; random_^=random_<<5; return float(random_>>8)/16777216.f; }
void ClientEffects::add_particles(HOBJECT object,uint32 count,LTVector p0,LTVector p1,LTVector v0,LTVector v1,LTVector c0,LTVector c1,float t0,float t1) {
    get(object,OT_PARTICLESYSTEM); valid(finite(p0) && finite(p1) && finite(v0) && finite(v1) && finite(c0) && finite(c1) && std::isfinite(t0) && std::isfinite(t1) && t0>=0 && t1>=t0 && t1<=1e12f);
    for (unsigned i=0;i<3;++i) valid(p0[i]<=p1[i] && v0[i]<=v1[i] && c0[i]<=c1[i]);
    if (count>budget-particle_count_) throw std::bad_alloc();
    for (uint32 i=0;i<count;++i) {
        std::array<float,6> r; for (auto& value : r) value=random();
        LTVector p,v,c; for (unsigned axis=0;axis<3;++axis) { p[axis]=std::lerp(p0[axis],p1[axis],r[axis]); v[axis]=std::lerp(v0[axis],v1[axis],r[axis+3]); }
        c.Init(std::lerp(c0.x,c1.x,r[0]),std::lerp(c0.y,c1.y,r[5]),std::lerp(c0.z,c1.z,r[3])); add_particle(object,p,v,c,std::lerp(t0,t1,r[3]));
    }
}
CParticle& ClientEffects::particle(HOBJECT object,LTParticle* particle) const { const auto& entry=get(object,OT_PARTICLESYSTEM); valid(entry.particles.contains(particle)); return *static_cast<CParticle*>(particle); }
bool ClientEffects::particles(HOBJECT object,LTParticle*& head,LTParticle*& tail) { get(object,OT_PARTICLESYSTEM); auto& system=*static_cast<CObjParticleSystem*>(object); head=system.m_Head.m_pNext; tail=&system.m_Head; return true; }
LTVector ClientEffects::particle_position(HOBJECT object,LTParticle* value) const { return particle(object,value).m_Pos; }
void ClientEffects::particle_position(HOBJECT object,LTParticle* value,LTVector position) { valid(finite(position)); particle(object,value).m_Pos=position; }
float ClientEffects::particle_lifetime(HOBJECT object,LTParticle* value,bool total) const { const auto& p=particle(object,value); return total ? p.m_TotalLifetime : p.m_Lifetime; }
void ClientEffects::remove_particle(HOBJECT object,LTParticle* value) {
    auto& p=particle(object,value); auto& system=*static_cast<CObjParticleSystem*>(object);
    p.m_pPrev->m_pNext=p.m_pNext; static_cast<CParticle*>(p.m_pNext)->m_pPrev=p.m_pPrev;
    objects_.at(object).particles.erase(value); --particle_count_; --system.m_u228; sync(system); p.~CParticle(); sb_Free(&system.m_pAttachMgr->m_Bank14,&p);
}
void ClientEffects::optimize(HOBJECT object) {
    auto& entry=get(object,OT_PARTICLESYSTEM); auto& system=*static_cast<CObjParticleSystem*>(object);
    LTVector low{0,0,0},high{0,0,0}; bool first=true;
    for (auto value : entry.particles) { const auto& p=particle(object,value); valid(finite(p.m_Pos)); for (unsigned i=0;i<3;++i) { low[i]=first ? p.m_Pos[i] : std::min(low[i],p.m_Pos[i]); high[i]=first ? p.m_Pos[i] : std::max(high[i],p.m_Pos[i]); } first=false; }
    system.m_vBoxMin=low; system.m_vBoxMax=high; const auto center=(low+high)*.5f;
    system.m_f208=center.x; system.m_f20c=center.y; system.m_f210=center.z; system.m_f214=(high-low).Mag()*.5f;
}
HLTLINE ClientEffects::add_line(HOBJECT object,const LTLine& line) {
    auto& entry=get(object,OT_LINESYSTEM); valid(finite(line)); if (lines_.size()>=budget) throw std::bad_alloc();
    auto& system=*static_cast<CObjLineSystem*>(object); auto* bank=&system.m_pAttachMgr->m_Bank30;
    auto* storage=sb_Allocate(bank); if (!storage) throw std::bad_alloc(); auto* node=new(storage) CLineSegment; const auto handle=reinterpret_cast<HLTLINE>(node);
    try { entry.lines.insert(handle); lines_.emplace(handle,object); }
    catch (...) { entry.lines.erase(handle); node->~CLineSegment(); sb_Free(bank,node); throw; }
    node->m_Line=line; node->m_pNext=&system.m_Head; node->m_pPrev=system.m_Head.m_pPrev; node->m_pPrev->m_pNext=node; system.m_Head.m_pPrev=node; sync(system); return handle;
}
HLTLINE ClientEffects::next_line(HOBJECT object,HLTLINE previous) const {
    const auto& entry=get(object,OT_LINESYSTEM); valid(!previous || entry.lines.contains(previous));
    const auto& system=*static_cast<CObjLineSystem*>(object); const auto* next=previous ? reinterpret_cast<CLineSegment*>(previous)->m_pNext : system.m_Head.m_pNext;
    return next==&system.m_Head ? nullptr : reinterpret_cast<HLTLINE>(const_cast<CLineSegment*>(next));
}
LTLine ClientEffects::line(HLTLINE handle) const { require_owner(); const auto found=lines_.find(handle); valid(found!=lines_.end()); get(found->second,OT_LINESYSTEM); return reinterpret_cast<CLineSegment*>(handle)->m_Line; }
void ClientEffects::line(HLTLINE handle,const LTLine& value) { valid(finite(value)); line(handle); reinterpret_cast<CLineSegment*>(handle)->m_Line=value; }
void ClientEffects::remove_line(HOBJECT object,HLTLINE handle) {
    auto& entry=get(object,OT_LINESYSTEM); valid(entry.lines.contains(handle)); auto& node=*reinterpret_cast<CLineSegment*>(handle); auto& system=*static_cast<CObjLineSystem*>(object);
    node.m_pPrev->m_pNext=node.m_pNext; node.m_pNext->m_pPrev=node.m_pPrev; sync(system); entry.lines.erase(handle); lines_.erase(handle); node.~CLineSegment(); sb_Free(&system.m_pAttachMgr->m_Bank30,&node);
}
ParticleSnapshot ClientEffects::snapshot_particles(HOBJECT object) const {
    const auto& entry=get(object,OT_PARTICLESYSTEM); ParticleSnapshot result; result.texture=entry.texture;
    if (entry.sprite) { const auto& animation=entry.sprite->animation(); result.texture=animation.frames[(entry.time/(1000/animation.frames_per_second))%animation.frames.size()]; if (animation.keyed) result.color_key=animation.color_key&0xffffff; }
    const auto& system=*static_cast<CObjParticleSystem*>(object); auto* current=system.m_Head.m_pNext; result.particles.reserve(entry.particles.size());
    while (current!=&system.m_Head) { valid(result.particles.size()<entry.particles.size()); const auto& p=particle(object,current); valid(finite(p.m_Pos) && finite(p.m_Color) && std::isfinite(p.m_Alpha) && std::isfinite(p.m_Size)); result.particles.push_back({p.m_Pos,p.m_Color,p.m_Alpha,p.m_Size}); current=p.m_pNext; }
    return result;
}
std::vector<LTLine> ClientEffects::snapshot_lines(HOBJECT object) const {
    const auto& entry=get(object,OT_LINESYSTEM); std::vector<LTLine> result; result.reserve(entry.lines.size());
    for (auto handle=next_line(object,nullptr); handle; handle=next_line(object,handle)) { valid(result.size()<entry.lines.size()); result.push_back(line(handle)); }
    return result;
}
bool ClientEffects::setup_grid(HOBJECT object,uint32 width,uint32 height,bool half) {
    get(object,OT_POLYGRID); valid(width>=2 && height>=2 && std::uint64_t(width)*height<=65000);
    auto& grid=*static_cast<CObjPolyGrid*>(object); const auto points=std::size_t(width)*height;
    if (points>4*1024*1024-(grid_points_-grid.m_Heights.size())) throw std::bad_alloc();
    std::vector<int8> heights(points); std::vector<uint16> indices; indices.reserve(std::size_t(width-1)*(height-1)*(half ? 3 : 6));
    for (uint32 y=0;y<height-1;++y) for (uint32 x=0;x<width-1;++x) {
        const auto i=y*width+x;
        for (auto index : {i+width+1,i+1,i}) indices.push_back(static_cast<uint16>(index));
        if (!half) for (auto index : {i+width,i+width+1,i}) indices.push_back(static_cast<uint16>(index));
    }
    grid_points_=grid_points_-grid.m_Heights.size()+points;
    grid.m_Heights.swap(heights); grid.m_Indices.swap(indices); grid.m_Width=width; grid.m_Height=height; return true;
}
LTRESULT ClientEffects::grid_texture(HOBJECT object,const char* name) {
    auto& entry=get(object,OT_POLYGRID); valid(name); auto key=platform::asset_key(name); valid(key.ends_with(".spr"));
    if (!assets_->contains(key)) return LT_NOTFOUND;
    auto sprite=sprites_.load(key); entry.texture=std::move(key); entry.sprite=std::move(sprite); entry.time=0; return LT_OK;
}
LTRESULT ClientEffects::grid_environment(HOBJECT object,const char* name) {
    auto& entry=get(object,OT_POLYGRID); if (!name) { entry.environment.clear(); return LT_OK; }
    auto key=platform::asset_key(name); valid(key.ends_with(".dtx")); if (!assets_->contains(key)) return LT_NOTFOUND;
    entry.environment=std::move(key); return LT_OK;
}
std::array<float,4> ClientEffects::grid_texture_info(HOBJECT object) const {
    get(object,OT_POLYGRID); const auto& grid=*static_cast<CObjPolyGrid*>(object); return {grid.m_XPan,grid.m_YPan,grid.m_XScale,grid.m_YScale};
}
void ClientEffects::grid_texture_info(HOBJECT object,float x_pan,float y_pan,float x_scale,float y_scale) {
    get(object,OT_POLYGRID); for (auto value : {x_pan,y_pan,x_scale,y_scale}) valid(std::isfinite(value) && std::fabs(value)<=1e12f);
    auto& grid=*static_cast<CObjPolyGrid*>(object); grid.m_XPan=x_pan; grid.m_YPan=y_pan; grid.m_XScale=x_scale; grid.m_YScale=y_scale;
}
void ClientEffects::grid_info(HOBJECT object,char*& data,uint32& width,uint32& height,PGColor*& colors) {
    get(object,OT_POLYGRID); auto& grid=*static_cast<CObjPolyGrid*>(object); data=reinterpret_cast<char*>(grid.m_Heights.data()); width=grid.m_Width; height=grid.m_Height; colors=grid.m_Colors.data();
}
void ClientEffects::fit_grid(HOBJECT object,LTVector min,LTVector max,LTVector* position,LTVector* scale) const {
    get(object,OT_POLYGRID); const auto& grid=*static_cast<CObjPolyGrid*>(object); valid(finite(min) && finite(max) && grid.m_Width>=2 && grid.m_Height>=2);
    for (unsigned i=0;i<3;++i) valid(min[i]<=max[i]);
    const auto center=(max-min)*.5f+min;
    if (position) *position=center;
    if (scale) scale->Init((max.x-min.x)/grid.m_Width,(max.y-center.y)/127,(max.z-min.z)/grid.m_Height);
}
PolygridSnapshot ClientEffects::snapshot_grid(HOBJECT object) const {
    const auto& entry=get(object,OT_POLYGRID); const auto& grid=*static_cast<CObjPolyGrid*>(object); PolygridSnapshot result;
    result.width=grid.m_Width; result.height=grid.m_Height; result.heights=grid.m_Heights; result.indices=grid.m_Indices; result.colors=grid.m_Colors;
    result.environment=entry.environment; result.x_pan=grid.m_XPan; result.y_pan=grid.m_YPan; result.x_scale=grid.m_XScale; result.y_scale=grid.m_YScale;
    if (entry.sprite) { const auto& animation=entry.sprite->animation(); result.texture=animation.frames[(entry.time/(1000/animation.frames_per_second))%animation.frames.size()]; if (animation.keyed) result.color_key=animation.color_key&0xffffff; }
    return result;
}
void ClientEffects::update(float elapsed,uint32 milliseconds,const std::function<std::optional<float>(LTVector)>& floor) {
    require_owner(); valid(std::isfinite(elapsed) && elapsed>=0 && elapsed<=1);
    for (auto& [object,entry] : objects_) {
        if (!entry.guard.unchanged() || (object->m_ObjectType!=OT_PARTICLESYSTEM && object->m_ObjectType!=OT_POLYGRID)) continue;
        entry.time+=milliseconds;
        if (object->m_ObjectType==OT_POLYGRID) continue;
        auto& system=*static_cast<CObjParticleSystem*>(object); const auto flags=system.m_u258;
        if (flags&PS_DUMB) continue;
        // Explicit Euler and the strict negative lifetime test match the game.
        auto* current=system.m_Head.m_pNext;
        while (current!=&system.m_Head) {
            auto& p=particle(object,current); auto* next=p.m_pNext;
            if (!(flags&PS_NEVERDIE)) p.m_Lifetime-=elapsed;
            if (!(flags&PS_NEVERDIE) && p.m_Lifetime<0) remove_particle(object,current);
            else { valid(finite(p.m_Velocity)); p.m_Pos+=p.m_Velocity*elapsed; p.m_Velocity.y+=elapsed*system.m_f248; valid(finite(p.m_Pos) && finite(p.m_Velocity)); }
            current=next;
        }
        if ((flags&PS_BOUNCE) && floor) if (const auto height=floor(object->m_Pos)) {
            for (auto value : entry.particles) { auto& p=particle(object,value); if (p.m_Velocity.y<0 && p.m_Pos.y<*height) { p.m_Pos.y=*height; p.m_Velocity.y*= -.5f; } }
        }
        optimize(object);
    }
}
}
