#include "engine/client/scene_renderer.h"
#include "engine/client/camera_view.h"
#include "engine/client/gl_models.h"
#include "engine/client/gl_sprites.h"
#include "engine/client/gl_effects.h"
#include "engine/client/gl_canvas.h"
#include "engine/client/gl_polygrids.h"
#include "engine/client/texture_link.h"
#include "engine/client/client_sprites.h"
#include "engine/server/model_callback.h"
#include "engine/server/world_state.h"
#include "engine/server/objtypes.h"
#include "rdev.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <set>
#include <unordered_map>

namespace nolf {
namespace {
bool finite(LTVector vector) { return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z); }
bool effect_type(uint8 type) { return type==OT_PARTICLESYSTEM || type==OT_LINESYSTEM || type==OT_CANVAS || type==OT_POLYGRID; }
struct Frame {
    explicit Frame(const CameraView& camera) {
        rd_PushAttrib(RD_ATTR_ALL);
        rd_MatrixMode(RD_PROJECTION); rd_PushMatrix();
        rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix();
        const auto rectangle=camera.rectangle();
        rd_Viewport(rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top);
        rd_Enable(RD_SCISSOR_TEST); rd_Scissor(rectangle.left,rectangle.top,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top);
        rd_Disable(RD_FOG); if (g_RDCaps.bDepthClamp) rd_Disable(RD_DEPTH_CLAMP); rd_DepthRange(.1f,1);
        load(camera);
    }
    static void load(const CameraView& camera) {
        rd_MatrixMode(RD_PROJECTION); rd_LoadMatrix(camera.projection().data());
        rd_MatrixMode(RD_MODELVIEW); rd_LoadMatrix(camera.view().data());
    }
    ~Frame() {
        if (g_RDCaps.bNearDepth) rd_NearDepth(0,0,0,0);
        rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix(); rd_MatrixMode(RD_PROJECTION); rd_PopMatrix(); rd_PopAttrib();
    }
};
std::uint8_t channel(float value) { return static_cast<std::uint8_t>(std::clamp(value,0.f,255.f)); }
void screen_quad() {
    rd_MatrixMode(RD_PROJECTION); rd_PushMatrix(); rd_LoadIdentity();
    rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadIdentity();
    rd_Begin(RD_QUADS); rd_Vertex2f(-1,-1); rd_Vertex2f(1,-1); rd_Vertex2f(1,1); rd_Vertex2f(-1,1); rd_End();
    rd_PopMatrix(); rd_MatrixMode(RD_PROJECTION); rd_PopMatrix(); rd_MatrixMode(RD_MODELVIEW);
}
// Fog for the world and objects, or for the sky: the sky is fogged all over
// by (far - 1)/(far - near) of its range, not by its distance. Answers
// whether fog is on.
bool apply_fog(const FogSettings& fog,bool sky) {
    if (!fog.enabled || (sky && !fog.sky)) { rd_Disable(RD_FOG); return false; }
    rd_FogColor(fog.color.x/255,fog.color.y/255,fog.color.z/255);
    if (sky) {
        auto fraction=fog.sky_far>fog.sky_near ? (fog.sky_far-1)/(fog.sky_far-fog.sky_near) : 0.f;
        fraction=std::max(fraction,0.f);
        if (fraction>=1) { rd_Disable(RD_FOG); return false; }
        constexpr float end=1e7f;
        rd_FogRange(fraction>0 ? end-end/fraction : -1e30f,end);
    } else rd_FogRange(fog.near_z,fog.far_z);
    rd_Enable(RD_FOG); return true;
}
// An object that asks for no fog draws without it; additive and multiply
// blends fog towards black.
struct ObjectFog {
    bool disabled=false,darkened=false; const FogSettings& fog;
    ObjectFog(const LTObject& object,bool on,const FogSettings& settings) : fog(settings) {
        if (!on) return;
        if (object.m_Flags&FLAG_FOGDISABLE) { rd_Disable(RD_FOG); disabled=true; }
        else if (object.m_Flags2&(FLAG2_ADDITIVE|FLAG2_MULTIPLY)) { rd_FogColor(0,0,0); darkened=true; }
    }
    ~ObjectFog() {
        if (disabled) rd_Enable(RD_FOG);
        if (darkened) rd_FogColor(fog.color.x/255,fog.color.y/255,fog.color.z/255);
    }
};
// The global light scale darkens the view (dst*scale, as dst*src + src*dst
// with src = scale/2) and the camera's light add brightens it.
void screen_passes(LTVector scale,LTVector add) {
    const auto off=[](float v) { return v<.99f || v>1.01f; };
    const bool scaled=off(scale.x) || off(scale.y) || off(scale.z),added=add.x>=.001f || add.y>=.001f || add.z>=.001f;
    if (!scaled && !added) return;
    rd_PushAttrib(RD_ATTR_ALL);
    rd_Disable(RD_TEXTURE_2D); rd_Disable(RD_FOG); rd_Disable(RD_DEPTH_TEST); rd_Disable(RD_ALPHA_TEST); rd_DepthMask(false); rd_Enable(RD_BLEND);
    if (scaled) { rd_BlendFunc(RD_DST_COLOR,RD_SRC_COLOR); rd_Color4f(scale.x*.5f,scale.y*.5f,scale.z*.5f,1); screen_quad(); }
    if (added) { rd_BlendFunc(RD_ONE,RD_ONE); rd_Color4f(add.x,add.y,add.z,1); screen_quad(); }
    rd_PopAttrib();
}
struct SkySpriteReport {
    BrushRenderStats* target;
    SpriteRenderStats value{};
    ~SkySpriteReport() { if (target) { target->objects+=value.sprites; target->triangles+=value.triangles; target->culled+=value.culled+value.missing; target->removed+=value.removed; } }
};
struct SkyModelReport {
    BrushRenderStats* target;
    ModelRenderStats value{};
    ~SkyModelReport() { if (target) { target->objects+=value.models; target->triangles+=value.triangles; target->culled+=value.culled; target->removed+=value.removed; } }
};
struct SkyEffectReport {
    BrushRenderStats* target;
    EffectRenderStats value{};
    ~SkyEffectReport() { if (target) { target->objects+=value.systems; target->triangles+=value.particles*2+value.triangles; target->culled+=value.culled+value.missing; target->removed+=value.removed; } }
};
}
struct SceneRenderer::State {
    std::shared_ptr<ClientScene> scene;
    std::shared_ptr<ClientTextures> textures;
    std::shared_ptr<const Assets> assets;
    ClientSprites sprites;
    GlTextures gpu;
    GlModels models{gpu};
    GlSprites sprite_draw{gpu};
    GlEffects effect_draw{gpu};
    GlCanvas canvas_draw{gpu};
    GlPolygrids grid_draw{gpu};
    std::shared_ptr<const CServerWorld> rendered_world;
    uint64 world_generation=0;
    std::unique_ptr<GlWorld> world_renderer;
    struct Material { std::shared_ptr<const TextureResource> texture; bool used=false; };
    std::unordered_map<std::string,Material> materials;
    const std::thread::id owner=std::this_thread::get_id();
    bool rendering=false,retired=false;
    State(std::shared_ptr<ClientScene> objects,std::shared_ptr<ClientTextures> images,std::shared_ptr<const Assets> assets)
        : scene(std::move(objects)),textures(std::move(images)),assets(std::move(assets)),sprites(this->assets) {
        if (!scene || !textures) throw std::invalid_argument("model scene renderer needs owned services");
    }
    void ready() const {
        if (owner!=std::this_thread::get_id()) throw std::logic_error("scene rendering used from another thread");
        if (retired || !rd_DeviceGeneration()) throw std::logic_error("scene renderer is unavailable");
    }
    std::shared_ptr<const TextureResource> material(std::string_view filename) {
        const auto name=platform::asset_key(filename); const auto found=materials.find(name);
        if (found!=materials.end()) { found->second.used=true; return found->second.texture; }
        auto texture=textures->load(name); materials.emplace(name,Material{texture,true}); return texture;
    }
    std::shared_ptr<const TextureResource> skin(std::string_view name,double time) {
        if (name.empty()) return {};
        if (name.ends_with(".spr")) {
            const auto sprite=sprites.load(name); const auto& animation=sprite->animation();
            if (animation.frames.empty()) return {};
            const auto period=1000/animation.frames_per_second;
            const auto cycle=double(period)*animation.frames.size();
            const auto frame=std::size_t(std::fmod(time*1000,cycle)/period);
            return material(animation.frames[frame]);
        }
        return material(name);
    }
};
namespace {
// A visible light, for models unless it only lights the world and for the
// world unless it only lights objects (r_DrawLight caps the world's at 40).
void add_light(const LTObject& light,SceneRenderOptions& options) {
    if (!(light.m_Flags&FLAG_VISIBLE) || !(light.m_LightRadius>0)) return;
    if (options.models.dynamic_lights && !(light.m_Flags&FLAG_ONLYLIGHTWORLD) && options.models.lights.size()<ModelLighting::max_lights*4)
        options.models.lights.push_back({light.m_Pos,light.m_LightRadius,{float(light.m_ColorR),float(light.m_ColorG),float(light.m_ColorB)}});
    if (options.models.dynamic_lights && !(light.m_Flags&FLAG_ONLYLIGHTOBJECTS) && options.world.lights.size()<40)
        options.world.lights.push_back(DynamicWorldLight::from(light.m_Pos,light.m_LightRadius,{light.m_ColorR,light.m_ColorG,light.m_ColorB},
            options.fast_lights || (light.m_Flags&FLAG_SOLIDLIGHT),light.m_Flags&FLAG_DONTLIGHTBACKFACING));
}
}
SceneRenderer::SceneRenderer(std::shared_ptr<ClientScene> scene,std::shared_ptr<ClientTextures> textures,std::shared_ptr<const Assets> assets)
    : state_(std::make_shared<State>(std::move(scene),std::move(textures),std::move(assets))) {}
SceneRenderer::~SceneRenderer() { state_->retired=true; }
ModelRenderStats SceneRenderer::render_models(HOBJECT camera,std::span<HOBJECT const> input,ModelRenderOptions options) {
    return draw_objects(camera,input,std::move(options),nullptr,nullptr,nullptr,nullptr,false,nullptr,nullptr);
}
SceneRenderStats SceneRenderer::render_objects(HOBJECT camera,std::span<HOBJECT const> input,SceneRenderOptions options) {
    const auto self=state_; self->ready(); if (input.size()>65536) throw std::invalid_argument("object list exceeds render budget");
    std::vector<HOBJECT> objects; objects.reserve(input.size()); SceneRenderStats stats;
    for (auto object : input) {
        if (!self->scene->contains(object)) throw std::invalid_argument("object list contains a foreign object");
        const auto type=object->m_ObjectType;
        if (type==OT_MODEL || type==OT_SPRITE || type==OT_WORLDMODEL || type==OT_CONTAINER || effect_type(type)) objects.push_back(object);
        else if (type==OT_LIGHT) add_light(*object,options);
        else if (type!=OT_NORMAL && type!=OT_CAMERA) ++stats.pending_objects;
    }
    options.world.draw_all=true;
    stats.models=draw_objects(camera,objects,std::move(options.models),&stats.world,&options.world,&stats.sprites,&stats.brushes,false,nullptr,&stats.effects);
    return stats;
}
SceneRenderStats SceneRenderer::render(HOBJECT camera,SceneRenderOptions options) {
    const auto self=state_; self->ready();
    if (!self->scene->contains(camera) || !self->scene->camera(camera)) throw std::invalid_argument("invalid scene camera");
    std::vector<HOBJECT> models; SceneRenderStats stats;
    const auto selected=options.world.draw_all ? self->scene->snapshot() : self->scene->visible(camera->m_Pos,options.models.far_distance);
    for (const auto& entry : selected) {
        if (!entry.guard.unchanged() || !self->scene->contains(entry.object)) continue;
        const auto type=entry.object->m_ObjectType;
        if (entry.object->m_Flags2&FLAG2_SKYOBJECT) { if (type!=OT_WORLDMODEL && type!=OT_CONTAINER && type!=OT_MODEL && type!=OT_SPRITE && type!=OT_NORMAL && !effect_type(type)) ++stats.pending_objects; continue; }
        if (type==OT_MODEL || type==OT_SPRITE || effect_type(type) || ((type==OT_WORLDMODEL || type==OT_CONTAINER) && entry.object!=self->scene->main_world())) models.push_back(entry.object);
        else if (type==OT_LIGHT) add_light(*entry.object,options);
        else if (type!=OT_CAMERA && type!=OT_NORMAL && entry.object!=self->scene->main_world()) ++stats.pending_objects;
    }
    // Sprites need render bounds from their frame dimensions rather than
    // collision dimensions. View models use camera coordinates.
    for (const auto& entry : self->scene->snapshot()) if (entry.guard.unchanged() && self->scene->contains(entry.object) &&
        !(entry.object->m_Flags2&FLAG2_SKYOBJECT) && entry.object!=self->scene->main_world() &&
        (entry.object->m_ObjectType==OT_SPRITE || effect_type(entry.object->m_ObjectType) || entry.object->m_ObjectType==OT_WORLDMODEL || entry.object->m_ObjectType==OT_CONTAINER || (entry.object->m_ObjectType==OT_MODEL && (entry.object->m_Flags&FLAG_REALLYCLOSE))) &&
        std::find(models.begin(),models.end(),entry.object)==models.end()) models.push_back(entry.object);
    stats.models=draw_objects(camera,models,std::move(options.models),&stats.world,&options.world,&stats.sprites,&stats.brushes,options.draw_world,&stats.sky,&stats.effects);
    return stats;
}
ModelRenderStats SceneRenderer::draw_objects(HOBJECT camera,std::span<HOBJECT const> input,ModelRenderOptions options,WorldRenderStats* world_stats,const WorldRenderOptions* world_options,SpriteRenderStats* sprite_stats,BrushRenderStats* brush_stats,bool draw_world,BrushRenderStats* sky_stats,EffectRenderStats* effect_stats) {
    const auto self=state_; self->ready();
    if (self->rendering) throw std::logic_error("recursive scene render");
    if (input.size()>65536 || !finite(options.light_scale) || !finite(options.model_add) || !std::isfinite(options.time) || options.time<0 || options.time>1e12)
        throw std::invalid_argument("invalid scene model render request");
    const auto active=self->scene->activate(); const auto settings=self->scene->camera(camera);
    if (!settings) throw std::invalid_argument("invalid scene camera");
    const auto normal=CameraView(camera->m_Pos,camera->m_Rotation,settings->rectangle,settings->fov_x,settings->fov_y,7,options.far_distance,options.horizontal_plus);
    const auto world=self->scene->world_data();
    std::optional<CameraView> sky_camera; std::vector<MoveObjectReference> sky_objects;
    std::set<HOBJECT,std::less<HOBJECT>> sky_members;
    if (world && draw_world && sky_stats) {
        const auto sky=self->scene->sky(); const auto position=sky_view_position(sky.definition,world->m_vMin,world->m_vMax,normal.position());
        sky_camera.emplace(position,camera->m_Rotation,settings->rectangle,settings->fov_x,settings->fov_y,.01f,options.far_distance,options.horizontal_plus);
        sky_objects=self->scene->sky_objects();
        for (const auto& entry : sky_objects) if (entry.guard.unchanged()) sky_members.insert(entry.object);
    }
    struct Object { MoveObjectReference reference; float distance; bool close,translucent,no_z,sprite,brush,sky,effect; };
    std::vector<Object> objects; objects.reserve(input.size()+maximum_sky_objects); std::set<HOBJECT,std::less<HOBJECT>> seen;
    for (auto object : input) {
        if (!self->scene->contains(object)) throw std::invalid_argument("render pass received a foreign object");
        if (sky_members.contains(object)) continue;
        const bool brush=brush_stats && (object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER);
        const bool effect=effect_stats && effect_type(object->m_ObjectType);
        if (object->m_ObjectType!=OT_MODEL && (!sprite_stats || object->m_ObjectType!=OT_SPRITE) && !brush && !effect) throw std::invalid_argument("render pass received an unsupported object type");
        if (!seen.insert(object).second || !(object->m_Flags&FLAG_VISIBLE) || (object->m_ChangeFlags&1)) continue;
        const bool sprite=object->m_ObjectType==OT_SPRITE;
        const bool close=(object->m_Flags&FLAG_REALLYCLOSE) && !brush && (!sprite || !(object->m_Flags&FLAG_ROTATEABLESPRITE));
        bool brush_translucent=false;
        if (brush && object->m_pWorldBsp) {
            const CWorldPart* first=nullptr;
            if (const auto* bsp=dynamic_cast<const CWorldBsp*>(object->m_pWorldBsp); bsp && bsp->m_nParts) first=bsp->m_ppParts[0];
            else if (const auto* terrain=dynamic_cast<const CWorldSimple*>(object->m_pWorldBsp); terrain && terrain->m_Array50.GetSize()) first=terrain->m_Array50[0];
            brush_translucent=first && (first->m_pSurface->m_nFlags&(8|0x80000));
        }
        const auto distance=close ? object->m_Pos.Mag() : (object->m_Pos-normal.position()).Mag();
        const auto metadata=self->scene->info(object); const bool solid_canvas=object->m_ObjectType==OT_CANVAS && metadata && (metadata->client_flags&CF_SOLIDCANVAS);
        const bool color_blend=object->m_ColorA<255 || bool(object->m_Flags2&(FLAG2_ADDITIVE|FLAG2_MULTIPLY));
        const bool translucent=object->m_ObjectType==OT_CANVAS ? !solid_canvas : object->m_ObjectType==OT_POLYGRID ? color_blend : sprite || effect || brush_translucent || color_blend;
        objects.push_back({MoveObjectReference(object),distance,close,translucent,sprite && bool(object->m_Flags&FLAG_SPRITE_NOZ),sprite,brush,false,effect});
    }
    for (const auto& entry : sky_objects) {
        if (!entry.guard.unchanged() || !self->scene->contains(entry.object)) continue;
        const auto* object=entry.object;
        if (!(object->m_Flags&FLAG_VISIBLE) || (object->m_ChangeFlags&1)) continue;
        const bool sprite=object->m_ObjectType==OT_SPRITE,brush=object->m_ObjectType==OT_WORLDMODEL || object->m_ObjectType==OT_CONTAINER;
        const bool effect=effect_stats && effect_type(object->m_ObjectType);
        if (!sprite && !brush && !effect && object->m_ObjectType!=OT_MODEL) continue;
        objects.push_back({entry,(object->m_Pos-sky_camera->position()).Mag(),false,sprite || effect || object->m_ColorA<255 || bool(object->m_Flags2&(FLAG2_ADDITIVE|FLAG2_MULTIPLY)),false,sprite,brush,true,effect});
    }
    std::stable_sort(objects.begin(),objects.end(),[](const Object& a,const Object& b) {
        if (a.sky!=b.sky) return a.sky;
        if (a.sky) return false; // Stable server slot order, including repeated IDs.
        if (a.no_z!=b.no_z) return !a.no_z;
        if (a.close!=b.close) return !a.close;
        if (a.translucent!=b.translucent) return !a.translucent;
        // Solid world models draw with the world, before the models whose
        // shadows fall on them.
        if (!a.translucent && a.brush!=b.brush) return a.brush;
        return a.translucent ? a.distance>b.distance : a.distance<b.distance;
    });
    self->rendering=true;
    for (auto& [name,material] : self->materials) material.used=false;
    struct Reset { State& state; ~Reset() { std::erase_if(state.materials,[](const auto& item) { return !item.second.used; }); state.rendering=false; } } reset{*self};
    Frame frame(normal); auto visibility=self->textures->begin_render(); ModelRenderStats stats;
    bool fog_sky=false,fog_on=apply_fog(options.fog,false);
    // What model shadows fall on: the drawn world, and the visible brushes.
    std::vector<ShadowReceiver> shadow_receivers;
    if (world && options.shadows.max>0) {
        for (auto* record : std::span(world->m_Worlds.GetArray(),world->m_Worlds.GetSize()))
            if (record->m_pWorldBsp->m_nUnknown48&0x20) shadow_receivers.push_back({record->m_pWorldBsp});
        for (const auto& entry : objects) if (entry.brush && !entry.sky && (entry.reference.object->m_Flags&FLAG_VISIBLE)) {
            auto* brush=static_cast<CObjWorldModel*>(entry.reference.object);
            if (brush->m_pWorldBsp) shadow_receivers.push_back({brush->m_pWorldBsp,&brush->m_Transform1f0,&brush->m_Transform230});
        }
    }
    std::shared_ptr<const WorldVisibility> sprite_visibility; WorldVisibility::Selection sprite_selection;
    if (sprite_stats && world && (!world_options || !world_options->draw_all)) for (auto* record : std::span(world->m_Worlds.GetArray(),world->m_Worlds.GetSize())) if (record->m_pWorldBsp->m_nUnknown48&0x20) {
        const auto& bsp=*record->m_pWorldBsp; sprite_visibility=bsp.m_Visibility ? bsp.m_Visibility : WorldVisibility::Build(bsp);
        std::vector<uint16> portals(bsp.m_nChunks); for (uint32 i=0;i<bsp.m_nChunks;++i) portals[i]=bsp.m_pChunks[i].m_Flags;
        sprite_selection=sprite_visibility->Select(normal.position(),portals); break;
    }
    if (!world) { self->world_renderer.reset(); self->rendered_world.reset(); self->world_generation=0; }
    else if (world_options) {
        if (world!=self->rendered_world || world->lighting_generation()!=self->world_generation) {
            auto renderer=std::make_unique<GlWorld>(world,self->textures,self->assets,self->gpu);
            self->world_renderer=std::move(renderer); self->rendered_world=world; self->world_generation=world->lighting_generation();
        }
        if (draw_world) *world_stats=self->world_renderer->render_static(normal,visibility,*world_options);
    }
    for (const auto& entry : objects) {
        if (entry.sky!=fog_sky) { fog_sky=entry.sky; fog_on=apply_fog(options.fog,fog_sky); }
        ObjectFog object_fog(*entry.reference.object,fog_on,options.fog);
        if (self->retired) { stats.retired=true; break; }
        if (!entry.reference.guard.unchanged() || !self->scene->contains(entry.reference.object)) { if (entry.sky) ++sky_stats->removed; else if (entry.sprite) ++sprite_stats->removed; else if (entry.brush) ++brush_stats->removed; else if (entry.effect) ++effect_stats->removed; else ++stats.removed; continue; }
        if (entry.sky && (!world_stats || !world_stats->sky)) continue;
        const auto& base=entry.sky ? *sky_camera : normal;
        rd_DepthRange(entry.sky ? 1.f : .1f,1);
        if (entry.brush) {
            if (!self->world_renderer || !world) { ++brush_stats->culled; continue; }
            auto style=*world_options; style.time=options.time;
            const auto result=entry.sky ? self->world_renderer->render_sky_brush(*static_cast<CObjWorldModel*>(entry.reference.object),base,visibility,style) :
                self->world_renderer->render_brush(*static_cast<CObjWorldModel*>(entry.reference.object),base,visibility,style,draw_world);
            auto& report=entry.sky ? *sky_stats : *brush_stats;
            report.polygons+=result.polygons; report.triangles+=result.triangles; report.lightmapped+=result.lightmapped; report.culled+=result.culled;
            if (result.polygons) ++report.objects;
            continue;
        }
        if (entry.effect) {
            SkyEffectReport sky_report{entry.sky ? sky_stats : nullptr}; auto& report=entry.sky ? sky_report.value : *effect_stats;
            auto* object=entry.reference.object; LTRotation identity; identity.Init();
            const auto view=entry.close ? CameraView({0,0,0},identity,settings->rectangle,settings->fov_x,settings->fov_y,g_RDCaps.bDepthClamp ? 7.f : .01f,options.far_distance,options.horizontal_plus) : base;
            Frame::load(view);
            if (g_RDCaps.bDepthClamp) { if (entry.close) rd_Enable(RD_DEPTH_CLAMP); else rd_Disable(RD_DEPTH_CLAMP); }
            if (g_RDCaps.bNearDepth) rd_NearDepth(entry.close ? .01f : 0,entry.close ? 7.f : 0,0,entry.close ? .1f : 0);
            EffectDrawStyle style; style.blend=object->m_Flags2&FLAG2_ADDITIVE ? ObjectBlend::Additive : object->m_Flags2&FLAG2_MULTIPLY ? ObjectBlend::Multiply : ObjectBlend::Alpha;
            std::size_t drawn=0;
            if (object->m_ObjectType==OT_POLYGRID) {
                const auto grid=self->scene->effects().snapshot_grid(object);
                if (grid.width<2 || grid.height<2 || grid.heights.empty()) { ++report.culled; continue; }
                const double radius=std::hypot(double(grid.width)*object->m_Scale.x*.5,double(object->m_Scale.y)*128,double(grid.height)*object->m_Scale.z*.5)+1;
                if (radius<=std::numeric_limits<float>::max() && !view.visible(object->m_Pos,static_cast<float>(radius))) { ++report.culled; continue; }
                std::shared_ptr<const TextureResource> texture,environment; PolygridDrawStyle draw_style; uint32 width=0,height=0;
                draw_style.environment_only=object->m_Flags&FLAG_ENVIRONMENTMAPONLY; draw_style.environment_add=world_options && world_options->environment_add;
                if (!draw_style.environment_only && !grid.texture.empty()) {
                    if (!self->assets->contains(grid.texture)) { ++report.missing; continue; }
                    texture=self->material(grid.texture); const auto& header=texture->snapshot()->texture.header; width=header.width; height=header.height;
                    draw_style.sampling.alpha=(header.flags&(0x100|0x80)) || header.format==TextureFormat::Dxt3 || header.format==TextureFormat::Dxt5 || header.format==TextureFormat::IndexedRgba;
                    draw_style.sampling.color_key=grid.color_key; draw_style.alpha_test=grid.color_key.has_value() || bool(header.flags&0x80);
                }
                // The game's environment map, else the texture's own (its DTX's
                // EnvMap link) under EnvMapPolyGrids.
                if (!world_options || world_options->polygrid_environment) {
                    if (!grid.environment.empty()) environment=self->material(grid.environment);
                    else if (!grid.texture.empty() && self->assets->contains(platform::asset_key(grid.texture))) {
                        const auto base=texture ? texture : self->material(grid.texture);
                        const auto link=texture_link(base->snapshot()->texture.header);
                        if (link.type==TextureLinkType::Environment && self->assets->contains(platform::asset_key(link.name))) environment=self->material(link.name);
                    }
                }
                if (draw_style.environment_only && !environment) { ++report.missing; continue; }
                draw_style.depth_write=!entry.translucent && !entry.sky;
                draw_style.blend=object->m_Flags2&FLAG2_ADDITIVE ? ObjectBlend::Additive : object->m_Flags2&FLAG2_MULTIPLY ? ObjectBlend::Multiply : entry.translucent ? ObjectBlend::Alpha : ObjectBlend::Opaque;
                const auto mesh=polygrid_mesh(grid,*object,view.position(),width,height,options.light_scale,world_options && world_options->saturate);
                if (!view.visible(mesh.center,mesh.radius)) { ++report.culled; continue; }
                const auto triangles=self->grid_draw.draw(mesh,texture,environment,visibility,draw_style);
                if (triangles) { ++report.systems; ++report.polygrids; report.triangles+=triangles; }
                continue;
            }
            if (object->m_ObjectType==OT_CANVAS) {
                const auto canvas=self->scene->canvas(object);
                if (!canvas || !canvas->draw || !view.visible(object->m_Pos,canvas->radius)) { ++report.culled; continue; }
                CanvasDraw commands([self](std::string_view name) -> std::shared_ptr<const TextureResource> { return self->assets->contains(name) ? self->material(name) : nullptr; });
                const auto scope=self->scene->activate(); canvas->draw(&commands,object,canvas->user);
                if (self->retired) { stats.retired=true; break; }
                if (!entry.reference.guard.unchanged() || !self->scene->contains(object)) { ++report.removed; continue; }
                Frame::load(view); const auto triangles=self->canvas_draw.draw(commands.commands(),visibility);
                if (triangles) { ++report.systems; ++report.canvases; report.triangles+=triangles; }
                continue;
            }
            if (object->m_ObjectType==OT_PARTICLESYSTEM) {
                const auto particles=self->scene->effects().snapshot_particles(object); std::shared_ptr<const TextureResource> texture; uint32 width=0,height=0;
                if (!particles.texture.empty()) {
                    if (!self->assets->contains(particles.texture)) { ++report.missing; continue; }
                    texture=self->material(particles.texture); const auto& header=texture->snapshot()->texture.header; width=header.width; height=header.height;
                    style.sampling.alpha=(header.flags&(0x100|0x80)) || header.format==TextureFormat::Dxt3 || header.format==TextureFormat::Dxt5 || header.format==TextureFormat::IndexedRgba;
                    style.sampling.color_key=particles.color_key; style.alpha_test=particles.color_key.has_value() || bool(header.flags&0x80);
                }
                const auto vertices=particle_vertices(particles,*object,view,width,height,entry.close || entry.sky ? .01f : 7.f,options.light_scale);
                drawn=self->effect_draw.draw(vertices,true,texture,visibility,style); report.particles+=drawn;
            } else {
                const auto lines=self->scene->effects().snapshot_lines(object); const auto vertices=line_vertices(lines,*object,view,options.light_scale);
                drawn=self->effect_draw.draw(vertices,false,{},visibility,style); report.lines+=drawn;
            }
            if (drawn) ++report.systems; else ++report.culled;
            continue;
        }
        if (entry.reference.object->m_ObjectType==OT_SPRITE) {
            SkySpriteReport sky_report{entry.sky ? sky_stats : nullptr}; auto& report=entry.sky ? sky_report.value : *sprite_stats;
            auto* object=static_cast<CObjSprite*>(entry.reference.object); const auto resource=self->scene->sprite(object);
            uint32 animation=0,index=0;
            if (!resource || object->m_Control.GetCurPos(animation,index)!=LT_OK || animation || index>=resource->animation().frames.size()) { ++report.missing; continue; }
            const auto& filename=resource->animation().frames[index];
            if (!self->assets->contains(filename)) { ++report.missing; continue; }
            const auto texture=self->material(filename); const auto pixels=texture->snapshot(); const auto& header=pixels->texture.header;
            const double radius=std::hypot(double(header.width)*object->m_Scale.x,double(header.height)*object->m_Scale.y)*((object->m_Flags&FLAG_GLOWSPRITE) ? 2 : 1);
            if (!entry.close && radius<=std::numeric_limits<float>::max() && (!base.visible(object->m_Pos,static_cast<float>(radius)) ||
                (!entry.sky && sprite_visibility && !sprite_visibility->Intersects(sprite_selection,object->m_Pos,static_cast<float>(radius))))) { ++report.culled; continue; }
            SpriteGeometryRequest geometry; geometry.position=object->m_Pos; geometry.rotation=object->m_Rotation; geometry.scale=object->m_Scale;
            geometry.width=header.width; geometry.height=header.height; geometry.flags=object->m_Flags; if (entry.sky) geometry.flags&=~FLAG_REALLYCLOSE;
            const auto clip=world && object->m_ClipPoly!=INVALID_HPOLY ? self->scene->polygon_geometry(object->m_ClipPoly,object->m_Pos) : std::nullopt;
            if (clip) geometry.clip_geometry=&*clip;
            const auto vertices=sprite_vertices(geometry,base); if (vertices.empty()) { ++report.culled; continue; }
            LTRotation identity; identity.Init();
            const auto view=entry.close ? CameraView({0,0,0},identity,settings->rectangle,settings->fov_x,settings->fov_y,g_RDCaps.bDepthClamp ? 7.f : .01f,options.far_distance,options.horizontal_plus) : base;
            Frame::load(view);
            if (g_RDCaps.bDepthClamp) { if (entry.close) rd_Enable(RD_DEPTH_CLAMP); else rd_Disable(RD_DEPTH_CLAMP); }
            if (g_RDCaps.bNearDepth) rd_NearDepth(entry.close ? .01f : 0,entry.close ? 7.f : 0,0,entry.close ? .1f : 0);
            const auto probe=entry.close ? normal.position()+normal.right()*object->m_Pos.x+normal.up()*object->m_Pos.y+normal.forward()*object->m_Pos.z : object->m_Pos;
            const auto shade=world && !(object->m_Flags&FLAG_NOLIGHT) ? world->m_Shade.Sample(probe) : std::array<uint8,4>{0,0,0,0};
            SpriteDrawStyle style;
            const std::array<uint8,3> color{object->m_ColorR,object->m_ColorG,object->m_ColorB};
            // r_CalcLightAdd: the lights near the sprite add to its colour; a
            // light colour below 128 takes light away.
            std::array<float,3> light_add{float(shade[2]),float(shade[1]),float(shade[0])};
            if (!(object->m_Flags&FLAG_NOLIGHT)) for (const auto& light : options.lights) {
                const auto distance=light.position.Dist(probe); if (distance>=light.radius) continue;
                const auto k=(1-distance/light.radius)*.7f;
                for (unsigned i=0;i<3;++i) light_add[i]+=(2*light.color[i]-255)*k;
            }
            for (unsigned i=0;i<3;++i) style.color[i]=channel(std::clamp(float(color[i])+light_add[i],0.f,255.f)*options.light_scale[i]);
            style.color[3]=object->m_ColorA; style.depth_test=!entry.no_z;
            style.blend=object->m_Flags2&FLAG2_ADDITIVE ? ObjectBlend::Additive : object->m_Flags2&FLAG2_MULTIPLY ? ObjectBlend::Multiply : ObjectBlend::Alpha;
            style.sampling.alpha=(header.flags&(0x100|0x80)) || header.format==TextureFormat::Dxt3 || header.format==TextureFormat::Dxt5 || header.format==TextureFormat::IndexedRgba;
            if (resource->animation().keyed) style.sampling.color_key=resource->animation().color_key&0xffffff;
            style.alpha_test=resource->animation().keyed || bool(header.flags&0x80);
            report.triangles+=self->sprite_draw.draw(vertices,texture,visibility,style); ++report.sprites; continue;
        }
        SkyModelReport sky_report{entry.sky ? sky_stats : nullptr}; auto& report=entry.sky ? sky_report.value : stats;
        auto* object=static_cast<ModelInstance*>(entry.reference.object); const auto info=self->scene->info(object);
        ModelCallbackGuard guard(object->m_AnimTracker);
        LTVector color{float(object->m_ColorR),float(object->m_ColorG),float(object->m_ColorB)},add=options.model_add;
        ModelHookData hook{}; hook.m_hObject=object; hook.m_Flags=MHF_USETEXTURE; hook.m_ObjectFlags=object->m_Flags; hook.m_LightAdd=&add; hook.m_ObjectColor=&color;
        if (options.hook) options.hook(&hook,options.hook_user);
        if (self->retired) { stats.retired=true; break; }
        if (!guard.unchanged() || !self->scene->contains(object)) { ++report.removed; continue; }
        if (!finite(color) || !finite(add)) throw std::invalid_argument("model hook returned nonfinite lighting");
        const auto pose=model_EvaluatePose(*object);
        if (self->retired) { stats.retired=true; break; }
        if (!pose || !guard.unchanged() || !self->scene->contains(object)) { ++report.removed; continue; }
        LTRotation identity; identity.Init();
        auto fov_x=settings->fov_x,fov_y=settings->fov_y;
        if (entry.close && pose->model->m_Unknown140) {
            const float minimum=std::numbers::pi_v<float>/18,maximum=std::numbers::pi_v<float>*17/18;
            fov_x=std::numbers::pi_v<float>-std::clamp(fov_x+pose->model->m_f144,minimum,maximum);
            fov_y=std::numbers::pi_v<float>-std::clamp(fov_y+pose->model->m_f148,minimum,maximum);
        }
        const auto view=entry.close ? CameraView({0,0,0},identity,settings->rectangle,fov_x,fov_y,g_RDCaps.bDepthClamp ? 7.f : .01f,options.far_distance,options.horizontal_plus) : base;
        const auto& distances=pose->model->m_Unknown08c; auto lod_distance=entry.distance;
        if (options.lod_offset && distances.GetSize()) {
            // ModelLODOffset moves the distance by that many levels' thresholds.
            const auto levels=std::min<uint32>(uint32(std::abs(options.lod_offset)),distances.GetSize());
            if (levels) lod_distance+=(options.lod_offset>0 ? 1 : -1)*distances[levels-1].m_f;
        }
        uint32 lod=0; for (uint32 i=0;i<distances.GetSize();++i) if (lod_distance>distances[i].m_f) lod=i+1;
        struct Mesh { const ModelPieceData* data; uint32 skin; std::vector<LTVector> positions; };
        std::vector<Mesh> meshes;
        LTVector center{pose->object.m[0][3],pose->object.m[1][3],pose->object.m[2][3]};
        double squared_radius=0;
        for (uint32 i=0;i<pose->model->m_Pieces.GetSize();++i) {
            if (i<32 && (pose->hidden_pieces&(1u<<i))) continue;
            const auto* piece=pose->model->m_Pieces[i]; if (!piece) continue;
            const auto* data=piece->GetPieceData(std::min(lod,piece->m_Array34.GetSize())); if (!data || data->triangles().empty()) continue;
            auto positions=model_SkinVertices(*pose,*data);
            for (const auto& point : positions) {
                double squared=0; for (unsigned axis=0;axis<3;++axis) { const double delta=double(point[axis])-center[axis]; squared+=delta*delta; }
                squared_radius=std::max(squared_radius,squared);
            }
            meshes.push_back({data,piece->m_Unknown24,std::move(positions)});
        }
        const auto radius=std::sqrt(squared_radius);
        if (!entry.close && radius<=std::numeric_limits<float>::max() && !view.visible(center,static_cast<float>(radius))) { ++report.culled; continue; }
        Frame::load(view);
        if (g_RDCaps.bDepthClamp) { if (entry.close) rd_Enable(RD_DEPTH_CLAMP); else rd_Disable(RD_DEPTH_CLAMP); }
        if (g_RDCaps.bNearDepth) rd_NearDepth(entry.close ? .01f : 0,entry.close ? 7.f : 0,0,entry.close ? .1f : 0);
        // A view model is posed in camera space and lit in the world.
        LTMatrix to_world; to_world.Identity();
        if (entry.close) for (unsigned row=0;row<3;++row) {
            to_world.m[row][0]=normal.right()[row]; to_world.m[row][1]=normal.up()[row]; to_world.m[row][2]=normal.forward()[row]; to_world.m[row][3]=normal.position()[row];
        }
        const auto lighting_position=entry.close ? normal.position()+normal.right()*center.x+normal.up()*center.y+normal.forward()*center.z : center;
        const auto k=options.saturation/255;
        const LTVector scale{color.x*k*options.light_scale.x,color.y*k*options.light_scale.y,color.z*k*options.light_scale.z};
        auto lighting=[&] {
            if (hook.m_ObjectFlags&FLAG_NOLIGHT) return ModelLighting::unlit(scale,add,object->m_ColorA);
            // A model with a NormalRef node is lit in that node's frame.
            auto frame=pose->object;
            const auto& model=*pose->model;
            if (model.m_Unknown13c && model.m_n0f4>=0 && std::size_t(model.m_n0f4)<pose->nodes.size()) {
                auto node=pose->nodes[model.m_n0f4]; auto reference=model.m_mNormalRef0fc; MatMul(&frame,&node,&reference);
            }
            if (entry.close) { auto local=frame; MatMul(&frame,&to_world,&local); }
            const auto shade=world ? world->m_Shade.Sample(lighting_position) : std::array<uint8,4>{0,0,0,0};
            ModelLighting result(frame,{float(shade[2]),float(shade[1]),float(shade[0])},scale,add,object->m_ColorA);
            const auto reach=float(std::min(radius,1e6));
            for (const auto& light : options.lights) result.add(light,lighting_position,reach,options.max_lights);
            if (world) for (const auto& light : world->Lights()) if (!light.fast) result.add(light,lighting_position,reach,options.max_lights);
            return result;
        }();
        std::vector<std::array<std::uint8_t,4>> vertex_colors;
        // Chrome (DoModelDrawing): the camera's right and up rows, through
        // the NormalRef node's frame when the model has one.
        std::shared_ptr<const TextureResource> chrome;
        if (options.chrome && !options.environment_map.empty() && (options.chrome_all || (hook.m_ObjectFlags&FLAG_ENVIRONMENTMAP))) chrome=self->skin(options.environment_map,options.time);
        std::array<LTVector,2> chrome_rows{normal.right(),normal.up()};
        const auto& chrome_model=*pose->model;
        if (chrome && chrome_model.m_Unknown13c && chrome_model.m_n0f4>=0 && std::size_t(chrome_model.m_n0f4)<pose->nodes.size()) {
            auto node=pose->nodes[chrome_model.m_n0f4]; auto reference=chrome_model.m_mNormalRef0fc; LTMatrix frame; MatMul(&frame,&node,&reference);
            if (entry.close) { auto local=frame; MatMul(&frame,&to_world,&local); }
            for (auto& row : chrome_rows) { const auto axis=row; row={axis.x*frame.m[0][0]+axis.y*frame.m[1][0]+axis.z*frame.m[2][0],axis.x*frame.m[0][1]+axis.y*frame.m[1][1]+axis.z*frame.m[2][1],axis.x*frame.m[0][2]+axis.y*frame.m[1][2]+axis.z*frame.m[2][2]}; }
        }
        const float chrome_scale=options.environment_scale!=0 ? .5f/options.environment_scale : .5f;
        const float chrome_u=normal.position().x*options.environment_pan+.5f,chrome_v=normal.position().z*options.environment_pan+.5f;
        std::vector<std::array<float,2>> chrome_uvs;
        bool drawn=false;
        for (const auto& mesh : meshes) {
            std::shared_ptr<const TextureResource> skin;
            if ((hook.m_Flags&MHF_USETEXTURE) && mesh.skin<info->skins.size()) skin=self->skin(info->skins[mesh.skin],options.time);
            ModelDrawStyle style; style.color=lighting.flat(); style.wireframe=hook.m_ObjectFlags&FLAG_MODELWIREFRAME;
            if (lighting.lights()) { lighting.light(mesh.data->vertices(),vertex_colors); style.vertex_colors=vertex_colors; }
            if (chrome) {
                chrome_uvs.clear();
                for (const auto& vertex : mesh.data->vertices())
                    chrome_uvs.push_back({chrome_rows[0].Dot(vertex.normal)*chrome_scale+chrome_u,chrome_rows[1].Dot(vertex.normal)*chrome_scale+chrome_v});
                auto chrome_style=style; chrome_style.vertex_uvs=chrome_uvs; chrome_style.alpha_test=false;
                if (chrome_style.blend==ModelBlend::Alpha) chrome_style.blend=ModelBlend::Opaque;
                report.triangles+=self->models.draw(*pose,*mesh.data,chrome,visibility,chrome_style,mesh.positions);
                if (options.chrome_all) { ++report.pieces; drawn=true; continue; }
                // The skin then blends over the chrome by its alpha.
                if (style.blend==ModelBlend::Opaque) { style.blend=ModelBlend::Alpha; style.sampling.alpha=true; }
            }
            style.depth_write=!entry.translucent && !entry.sky; style.alpha_test=!entry.translucent && bool(object->m_Flags2&FLAG2_CHROMAKEY);
            style.blend=object->m_Flags2&FLAG2_ADDITIVE ? ModelBlend::Additive : object->m_Flags2&FLAG2_MULTIPLY ? ModelBlend::Multiply : entry.translucent ? ModelBlend::Alpha : ModelBlend::Opaque;
            if (skin) {
                const auto& header=skin->snapshot()->texture.header;
                style.sampling.alpha=style.alpha_test || (header.flags&(0x100|0x80|1)) || header.format==TextureFormat::Dxt3 || header.format==TextureFormat::Dxt5 || header.format==TextureFormat::IndexedRgba;
            }
            report.triangles+=self->models.draw(*pose,*mesh.data,skin,visibility,style,mesh.positions); ++report.pieces; drawn=true;
        }
        if (drawn) ++report.models;
        // The shadow, after the model, on the hook's flags.
        if (drawn && !entry.close && !entry.sky && (hook.m_ObjectFlags&FLAG_SHADOW))
            model_DrawShadow(*pose->model,object->m_Pos,shadow_receivers,normal.position(),options.shadows);
    }
    if (world_options && !stats.retired) {
        const auto view_camera=self->scene->camera(camera);
        screen_passes(options.screen_scale,options.screen_add && view_camera ? view_camera->light_add : LTVector(0,0,0));
    }
    if (!stats.retired) visibility.finish();
    return stats;
}
}
