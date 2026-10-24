#pragma once
#include "engine/client/client_sprites.h"
#include "engine/server/object_lifetime.h"
#include "iltclient.h"
#include <functional>
#include <map>
#include <optional>
#include <set>

struct CParticle;
namespace nolf {
struct EffectParticle {
    LTVector position{0,0,0},color{255,255,255};
    float alpha=1,size=1;
};
struct ParticleSnapshot {
    std::string texture;
    std::optional<uint32> color_key;
    std::vector<EffectParticle> particles;
};
struct PolygridSnapshot {
    uint32 width=0,height=0;
    std::vector<int8> heights;
    std::vector<uint16> indices;
    std::array<PGColor,256> colors{};
    std::string texture,environment;
    std::optional<uint32> color_key;
    float x_pan=0,y_pan=0,x_scale=1,y_scale=1;
};
// Simulation and SDK record ownership. Rendering consumes owned snapshots;
// platform drawing and device objects never enter this service.
class ClientEffects {
public:
    explicit ClientEffects(std::shared_ptr<const Assets>);
    void add_object(HOBJECT);
    void forget(HOBJECT);
    void clear();
    LTRESULT setup(HOBJECT,const char*,float gravity,uint32 flags,float radius);
    LTRESULT software_color(HOBJECT,float,float,float);
    LTParticle* add_particle(HOBJECT,LTVector,LTVector,LTVector,float);
    void add_particles(HOBJECT,uint32,LTVector,LTVector,LTVector,LTVector,LTVector,LTVector,float,float);
    bool particles(HOBJECT,LTParticle*&,LTParticle*&);
    LTVector particle_position(HOBJECT,LTParticle*) const;
    void particle_position(HOBJECT,LTParticle*,LTVector);
    float particle_lifetime(HOBJECT,LTParticle*,bool total) const;
    void remove_particle(HOBJECT,LTParticle*);
    void optimize(HOBJECT);
    HLTLINE add_line(HOBJECT,const LTLine&);
    HLTLINE next_line(HOBJECT,HLTLINE) const;
    LTLine line(HLTLINE) const;
    void line(HLTLINE,const LTLine&);
    void remove_line(HOBJECT,HLTLINE);
    ParticleSnapshot snapshot_particles(HOBJECT) const;
    std::vector<LTLine> snapshot_lines(HOBJECT) const;
    bool setup_grid(HOBJECT,uint32,uint32,bool half_triangles);
    LTRESULT grid_texture(HOBJECT,const char*);
    LTRESULT grid_environment(HOBJECT,const char*);
    std::array<float,4> grid_texture_info(HOBJECT) const;
    void grid_texture_info(HOBJECT,float,float,float,float);
    void grid_info(HOBJECT,char*&,uint32&,uint32&,PGColor*&);
    void fit_grid(HOBJECT,LTVector min,LTVector max,LTVector* position,LTVector* scale) const;
    PolygridSnapshot snapshot_grid(HOBJECT) const;
    void update(float,uint32 milliseconds,const std::function<std::optional<float>(LTVector)>& floor);
private:
    struct Entry {
        explicit Entry(HOBJECT object) : guard(*object) {}
        ObjectCallbackGuard guard;
        std::string texture;
        std::string environment;
        std::shared_ptr<SpriteResource> sprite;
        std::uint64_t time=0;
        std::set<LTParticle*,std::less<LTParticle*>> particles;
        std::set<HLTLINE,std::less<HLTLINE>> lines;
    };
    Entry& get(HOBJECT,uint8 type);
    const Entry& get(HOBJECT,uint8 type) const;
    CParticle& particle(HOBJECT,LTParticle*) const;
    void require_owner() const;
    float random();
    std::shared_ptr<const Assets> assets_;
    ClientSprites sprites_;
    std::map<HOBJECT,Entry,std::less<HOBJECT>> objects_;
    std::map<HLTLINE,HOBJECT,std::less<HLTLINE>> lines_;
    std::size_t particle_count_=0;
    std::size_t grid_points_=0;
    uint32 random_=0x6e6f6c66;
    const std::thread::id owner_=std::this_thread::get_id();
};
}
