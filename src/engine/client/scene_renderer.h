#pragma once
#include "engine/client/client_scene.h"
#include "engine/client/client_textures.h"
#include "engine/client/gl_world.h"
#include "engine/client/model_lighting.h"
#include "engine/client/model_shadows.h"

namespace nolf {
// FogEnable, FogR/G/B (0..255), FogNearZ/FarZ and the sky's fog.
struct FogSettings {
    bool enabled=false,sky=true;
    LTVector color{255,255,255};
    float near_z=0,far_z=2000,sky_near=0,sky_far=2000;
};
struct ModelRenderOptions {
    // A game module supplying hooks stays loaded throughout the render call.
    std::shared_ptr<const void> code;
    ModelHookFn hook=nullptr;
    void* hook_user=nullptr;
    LTVector light_scale{1,1,1},model_add{0,0,0};
    // The frame's visible dynamic lights; render() gathers them itself
    // unless dynamic lights are off.
    std::vector<ModelLightSource> lights;
    std::size_t max_lights=4;
    bool dynamic_lights=true;
    // Model colour scale under Saturate (ModelSaturation), and the LOD
    // distance shift in levels (ModelLODOffset).
    float saturation=1;
    int lod_offset=0;
    // A scene ends with the global light scale multiplied over the view and
    // the camera's light add added (LightAddPoly).
    LTVector screen_scale{1,1,1};
    bool screen_add=true;
    FogSettings fog;
    ModelShadowSettings shadows;
    // Chrome: the EnvMap texture under FLAG_ENVIRONMENTMAP models
    // (EnvMapEnable; EnvMapAll for every model, alone), EnvScale, EnvPanSpeed.
    std::string environment_map;
    bool chrome=false,chrome_all=false;
    float environment_scale=1,environment_pan=.0005f;
    float far_distance=10000;
    double time=0;
    bool horizontal_plus=true;
};
struct ModelRenderStats {
    std::size_t models=0,pieces=0,triangles=0,culled=0,removed=0;
    bool retired=false;
};
struct SceneRenderOptions { ModelRenderOptions models; WorldRenderOptions world; bool draw_world=true,fast_lights=false; };
struct SpriteRenderStats { std::size_t sprites=0,triangles=0,culled=0,missing=0,removed=0; };
struct BrushRenderStats { std::size_t objects=0,polygons=0,triangles=0,lightmapped=0,culled=0,removed=0; };
struct EffectRenderStats { std::size_t systems=0,particles=0,lines=0,canvases=0,polygrids=0,triangles=0,culled=0,missing=0,removed=0; };
struct SceneRenderStats { ModelRenderStats models; WorldRenderStats world; SpriteRenderStats sprites; BrushRenderStats brushes,sky; EffectRenderStats effects; std::size_t pending_objects=0; };
// Owns world, model, sprite, brush, sky and effect passes with shared texture visibility.
class SceneRenderer {
    struct State;
public:
    SceneRenderer(std::shared_ptr<ClientScene>,std::shared_ptr<ClientTextures>,std::shared_ptr<const Assets>);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&)=delete;
    SceneRenderer& operator=(const SceneRenderer&)=delete;
    ModelRenderStats render_models(HOBJECT camera,std::span<HOBJECT const>,ModelRenderOptions = {});
    SceneRenderStats render_objects(HOBJECT camera,std::span<HOBJECT const>,SceneRenderOptions = {});
    SceneRenderStats render(HOBJECT camera,SceneRenderOptions = {});
private:
    ModelRenderStats draw_objects(HOBJECT,std::span<HOBJECT const>,ModelRenderOptions,WorldRenderStats*,const WorldRenderOptions*,SpriteRenderStats*,BrushRenderStats*,bool,BrushRenderStats*,EffectRenderStats*);
    std::shared_ptr<State> state_;
};
}
