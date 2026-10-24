#pragma once
#include "engine/client/camera_view.h"
#include "engine/client/gl_textures.h"
#include "engine/client/render_blend.h"
#include "engine/client/world_lightmaps.h"

class CServerWorld;
class CObjWorldModel;
namespace nolf {
struct WorldRenderOptions {
    LTVector light_scale{1,1,1};
    // The frame's dynamic lights on the world, ramped by LightSaturate.
    std::vector<DynamicWorldLight> lights;
    float light_saturate=1;
    // The visible world models where they are now, for the lights on their
    // lightmaps.
    std::vector<LightmapPlacement> placements;
    // DetailTextures, DetailTextureAdd (added signed, else modulating),
    // DetailTextureScale and EnvMapWorld.
    bool detail_textures=true,detail_add=true,environment_maps=true,polygrid_environment=true;
    float detail_scale=.2f;
    // The sky's cloud shadow on SURF_PANNINGSKY polygons: planar on world
    // X/Z, offset and scaled.
    std::string pan_texture;
    std::array<float,2> pan_offset{},pan_scale{};
    TextureSampling sampling;
    double time=0;
    bool lightmaps=true,saturate=false,fix_tjunctions=true,cull=true,draw_all=false,environment_add=false;
};
struct WorldRenderStats {
    std::size_t regions=0,candidates=0,polygons=0,triangles=0,lightmapped=0,culled=0,sky=0,missing_materials=0;
};
// Canonical BSP material and geometry passes. The scene renderer supplies
// cameras and orders static geometry, movable brushes and sky layers.
class GlWorld {
public:
    GlWorld(std::shared_ptr<const CServerWorld>,std::shared_ptr<ClientTextures>,std::shared_ptr<const Assets>,GlTextures&);
    ~GlWorld();
    GlWorld(const GlWorld&)=delete;
    GlWorld& operator=(const GlWorld&)=delete;
    WorldRenderStats render_static(const CameraView&,ClientTextures::Render&,WorldRenderOptions = {});
    WorldRenderStats render_brush(const CObjWorldModel&,const CameraView&,ClientTextures::Render&,WorldRenderOptions = {},bool static_already_drawn=false);
    WorldRenderStats render_sky_brush(const CObjWorldModel&,const CameraView&,ClientTextures::Render&,WorldRenderOptions = {});
    std::vector<std::string> missing_materials() const;
private:
    struct State;
    std::unique_ptr<State> state_;
    WorldRenderStats draw_brush(const CObjWorldModel&,const CameraView&,ClientTextures::Render&,WorldRenderOptions,bool,bool);
};
}
