#pragma once
#include "engine/portable/image.h"
#include "engine/server/world_polygon.h"
#include "engine/server/world_lighting.h"
#include <memory>
#include <array>
#include <optional>
#include <span>

class CServerWorld;
namespace nolf {
struct LightmapTile {
    uint32 key=0;
    uint16 width=0,height=0;
    LTVector origin{0,0,0},right{1,0,0},up{0,1,0};
    float grid=1;
    // The polygon's plane, which dynamic lights are tested against.
    LTVector normal{0,0,1}; float distance=0;
    std::vector<WorldLightmapReference> layers;
};
struct LightmapLocation { std::size_t page=0; uint32 x=0,y=0,width=0,height=0; };
// Where a world model's BSP is now: its index among the world's models and
// world space into the BSP's own, which its lightmap texels are placed in.
struct LightmapPlacement { uint16 model=0; LTMatrix to_local; };
// A dynamic light on the world (r_DrawLight): it adds 2*colour - 255 scaled
// by its falloff, or a solid light (FLAG_SOLIDLIGHT, FastLight) sets its
// colour; FLAG_DONTLIGHTBACKFACING spares what faces away.
struct DynamicWorldLight {
    LTVector position{0,0,0}; float radius=0;
    std::array<float,3> add{}; std::array<uint8,3> color{};
    bool solid=false,front_only=false;
    static DynamicWorldLight from(LTVector position,float radius,std::array<uint8,3> color,bool solid,bool front_only);
    bool touches(LTVector normal,float distance,LTVector centre,float extent) const;
    // A colour (0..255) at point, LightSaturate ramping the falloff.
    void apply(LTVector point,float saturate,std::array<int,3>& color) const;
};
// CPU lightmap composition and atlas packing. GPU residency belongs to the
// device adapter. All geometry metadata and layer references are owned here.
class WorldLightmaps {
public:
    WorldLightmaps(std::shared_ptr<const CWorldData>,std::vector<LightmapTile>,
        uint32 page_size=512,std::size_t budget=256*1024*1024);
    ~WorldLightmaps();
    WorldLightmaps(const WorldLightmaps&)=delete;
    WorldLightmaps& operator=(const WorldLightmaps&)=delete;
    // Composes what the light animations changed, with these lights added to
    // the tiles they reach (those lit before get their own lightmaps back).
    // A placed model's tiles take the lights in its BSP's space.
    std::size_t update(std::span<const DynamicWorldLight> lights = {},float saturate=1,std::span<const LightmapPlacement> placements = {});
    std::size_t pages() const;
    const Image& image(std::size_t page) const;
    std::uint64_t revision(std::size_t page) const;
    std::optional<LightmapLocation> find(uint32 key) const;
    std::array<float,2> coordinates(uint32 key,LTVector point) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
std::unique_ptr<WorldLightmaps> world_lightmaps(std::shared_ptr<const CServerWorld>,
    uint32 page_size=512,std::size_t budget=256*1024*1024);
}
