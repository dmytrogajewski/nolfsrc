#include "engine/client/gl_world.h"
#include "engine/client/gl_programs.h"
#include "engine/client/texture_link.h"
#include "engine/client/gl_lightmaps.h"
#include "engine/client/client_sprites.h"
#include "engine/server/world_state.h"
#include "engine/server/ltobject_internal.h"
#include "engine/server/world_polygon_geometry.h"
#include "engine/portable/string.h"
#include "rdev.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace nolf {
namespace {
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
struct Entry { uint32 key; const CWorldPart* polygon; const CWorldBsp* model; };
struct Style { const LTMatrix* transform=nullptr; std::array<uint8,4> color{255,255,255,255}; ObjectBlend blend=ObjectBlend::Opaque; bool chroma=false,record_static=false,sky=false; };
struct Frame {
    unsigned units;
    explicit Frame(const CameraView& camera) : units(std::min(3,g_RDCaps.nTextureUnits)) {
        rd_PushAttrib(RD_ATTR_ALL);
        for (unsigned unit=0;unit<units;++unit) { rd_ActiveTexture(unit); rd_MatrixMode(RD_TEXTURE); rd_PushMatrix(); rd_LoadIdentity(); }
        for (int unit=units;unit<g_RDCaps.nTextureUnits;++unit) { rd_ActiveTexture(unit); rd_Disable(RD_TEXTURE_2D); }
        rd_ActiveTexture(0); rd_MatrixMode(RD_PROJECTION); rd_PushMatrix(); rd_LoadMatrix(camera.projection().data());
        rd_MatrixMode(RD_MODELVIEW); rd_PushMatrix(); rd_LoadMatrix(camera.view().data());
        const auto rect=camera.rectangle(); rd_Viewport(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top);
        rd_Enable(RD_SCISSOR_TEST); rd_Scissor(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top);
        rd_Enable(RD_DEPTH_TEST); rd_DepthFunc(RD_LEQUAL); rd_DepthMask(true); rd_ColorMask(true); rd_DepthRange(.1f,1);
        rd_Disable(RD_BLEND); rd_CullBack(false);   // fog stays as the scene set it
        if (g_RDCaps.bDepthClamp) rd_Disable(RD_DEPTH_CLAMP);
    }
    ~Frame() {
        rd_MatrixMode(RD_MODELVIEW); rd_PopMatrix(); rd_MatrixMode(RD_PROJECTION); rd_PopMatrix();
        for (unsigned unit=0;unit<units;++unit) { rd_ActiveTexture(unit); rd_MatrixMode(RD_TEXTURE); rd_PopMatrix(); }
        rd_PopAttrib();
    }
};
uint8 channel(double value) { return static_cast<uint8>(std::clamp(value,0.,255.)); }
LTVector transform(const LTMatrix& matrix,LTVector source) {
    return world_TransformPoint(matrix,source);
}
// A dynamic light on a vertex-lit polygon per pixel: the add r_LightData
// gives a lightmap texel, over the texture as the vertex colour takes it
// (and the cloud shadow where one is), added onto the polygon drawn without
// it. Unquantised, the pool is as smooth as the pixels.
constexpr const char* light_vertex=R"(#version 120
varying vec3 v_local;
varying vec2 v_uv;
varying vec2 v_pan;
void main() {
    v_local=gl_Vertex.xyz; v_uv=gl_MultiTexCoord0.xy; v_pan=gl_MultiTexCoord1.xy;
    vec4 eye=gl_ModelViewMatrix*gl_Vertex;
    gl_FogFragCoord=abs(eye.z);
    gl_Position=gl_ProjectionMatrix*eye;
}
)";
constexpr const char* light_fragment=R"(#version 120
uniform sampler2D u_texture;
uniform sampler2D u_pan;
uniform vec4 u_light;
uniform vec3 u_add;
uniform vec3 u_scale;
uniform float u_saturate;
uniform float u_textured;
uniform float u_alpha_test;
uniform float u_panned;
uniform float u_fog;
varying vec3 v_local;
varying vec2 v_uv;
varying vec2 v_pan;
void main() {
    vec3 d=v_local-u_light.xyz; float falloff=1.0-dot(d,d)/(u_light.w*u_light.w);
    if (falloff<=0.0) discard;
    vec4 texel=u_textured>0.5 ? texture2D(u_texture,v_uv) : vec4(1.0);
    if (u_alpha_test>0.5 && texel.a<=0.1666) discard;
    vec3 c=texel.rgb*u_add*min(1.0,falloff*u_saturate)*u_scale;
    if (u_panned>0.0) c*=texture2D(u_pan,v_pan).rgb*u_panned;
    if (u_fog>0.5) c*=clamp((gl_Fog.end-gl_FogFragCoord)*gl_Fog.scale,0.0,1.0);
    gl_FragColor=vec4(c,1.0);
}
)";
bool facing(const CWorldPart& polygon,LTVector eye) {
    const auto* plane=polygon.m_pSurface->m_pPlane;
    return !plane || double(plane->m_Normal.x)*eye.x+double(plane->m_Normal.y)*eye.y+double(plane->m_Normal.z)*eye.z-plane->m_Dist>.01;
}
}
struct GlWorld::State {
    std::shared_ptr<const CServerWorld> world;
    std::shared_ptr<ClientTextures> textures;
    std::shared_ptr<const Assets> assets;
    GlTextures& gpu;
    ClientSprites sprites;
    std::shared_ptr<WorldLightmaps> atlas;
    GlLightmaps lightmaps;
    const uint64 generation;
    const std::thread::id owner=std::this_thread::get_id();
    std::unordered_map<std::string,std::shared_ptr<const TextureResource>> materials;
    std::unordered_map<std::string,std::shared_ptr<const SpriteResource>> animations;
    std::unordered_set<std::string> missing;
    std::unordered_set<uint32> static_polygons;
    uint64 lighting_render=0;
    GlProgram light_program{"world light",light_vertex,light_fragment};
    void draw(std::span<const Entry>,const CameraView&,ClientTextures::Render&,const WorldRenderOptions&,WorldRenderStats&,const Style&);
    State(std::shared_ptr<const CServerWorld> level,std::shared_ptr<ClientTextures> images,std::shared_ptr<const Assets> source,GlTextures& device)
        : world(std::move(level)),textures(std::move(images)),assets(std::move(source)),gpu(device),sprites(assets),
          atlas(world_lightmaps(world)),lightmaps(atlas),generation(world->lighting_generation()) {
        if (!textures || !assets) throw std::invalid_argument("static world renderer needs owned services");
    }
    void live() const {
        if (owner!=std::this_thread::get_id()) throw std::logic_error("world rendering used from another thread");
        if (!rd_DeviceGeneration() || world->lighting_generation()!=generation) throw std::logic_error("world renderer or source is unavailable");
    }
    std::unordered_map<const TextureResource*,TextureLink> links;
    const TextureLink& link(const TextureResource& texture,const TexturePixels& pixels) {
        auto [it,inserted]=links.try_emplace(&texture);
        if (inserted) it->second=texture_link(pixels.texture.header);
        return it->second;
    }
    std::shared_ptr<const TextureResource> material(std::string_view filename,double time) {
        if (filename.empty()) return {};
        auto name=platform::asset_key(filename);
        if (name.ends_with(".spr")) {
            auto [it,inserted]=animations.try_emplace(name);
            if (inserted) {
                if (!assets->contains(name)) { missing.insert(name); return {}; }
                try { it->second=sprites.load(name); } catch (...) { animations.erase(it); throw; }
            }
            if (!it->second) return {};
            const auto& animation=it->second->animation(); if (animation.frames.empty()) return {};
            const auto period=1000/animation.frames_per_second;
            name=platform::asset_key(animation.frames[std::size_t(std::fmod(time*1000,double(period)*animation.frames.size())/period)]);
        }
        const auto found=materials.find(name); if (found!=materials.end()) return found->second;
        if (!assets->contains(name)) { missing.insert(name); materials.emplace(std::move(name),nullptr); return {}; }
        auto result=textures->load(name); materials.emplace(std::move(name),result); return result;
    }
};
GlWorld::GlWorld(std::shared_ptr<const CServerWorld> world,std::shared_ptr<ClientTextures> textures,std::shared_ptr<const Assets> assets,GlTextures& gpu)
    : state_(std::make_unique<State>(std::move(world),std::move(textures),std::move(assets),gpu)) {}
GlWorld::~GlWorld()=default;
std::vector<std::string> GlWorld::missing_materials() const {
    if (state_->owner!=std::this_thread::get_id()) throw std::logic_error("world materials queried from another thread");
    std::vector<std::string> names(state_->missing.begin(),state_->missing.end()); std::sort(names.begin(),names.end()); return names;
}
WorldRenderStats GlWorld::render_static(const CameraView& camera,ClientTextures::Render& render,WorldRenderOptions options) {
    auto& state=*state_; state.live();
    if (!finite(options.light_scale) || !std::isfinite(options.time) || options.time<0 || options.time>1e12)
        throw std::invalid_argument("invalid static world render settings");
    std::vector<Entry> queue; std::unordered_set<uint32> seen; WorldRenderStats stats;
    auto add=[&](CWorldPolyKey key) {
        const uint32 id=uint32(key.m_iModel)<<16|key.m_iPoly;
        if (!seen.insert(id).second) return;
        if (key.m_iModel>=state.world->m_Worlds.GetSize()) throw std::invalid_argument("static polygon model key outside world");
        const auto* model=state.world->m_Worlds[key.m_iModel]->m_pWorldBsp;
        if (key.m_iPoly>=model->m_nParts) throw std::invalid_argument("static polygon key outside model");
        const auto* polygon=model->m_ppParts[key.m_iPoly]; ++stats.candidates;
        const auto& surface=*polygon->m_pSurface;
        if (surface.m_nFlags&4) return;
        if (surface.m_nFlags&16) { ++stats.sky; return; }
        const auto* plane=surface.m_pPlane;
        if ((options.cull && plane && double(plane->m_Normal.x)*camera.position().x+double(plane->m_Normal.y)*camera.position().y+double(plane->m_Normal.z)*camera.position().z-plane->m_Dist<=.01) ||
            !camera.visible(polygon->m_vCentre,polygon->m_fRadius)) { ++stats.culled; return; }
        queue.push_back({id,polygon,model});
    };
    auto region=[&](const CWorldGroupQT1& group) { ++stats.regions; for (const auto& key : std::span(group.m_pRefs,group.m_nRefs)) add(key); };
    CWorldBsp* visibility=nullptr;
    for (auto* record : std::span(state.world->m_Worlds.GetArray(),state.world->m_Worlds.GetSize())) if (record->m_pWorldBsp->m_nUnknown48&0x20) { visibility=record->m_pWorldBsp; break; }
    if (visibility && visibility->m_n8c) {
        if (options.draw_all) for (const auto& group : std::span(visibility->m_p88,visibility->m_n8c)) region(group);
        else { CWorldVisQuery query; query.position=camera.position(); query.region=region; if (visibility->DoVisQuery(query)!=LT_OK) throw std::runtime_error("native static world visibility query failed"); }
    } else for (uint32 model=0;model<state.world->m_Worlds.GetSize();++model) {
        const auto* bsp=state.world->m_Worlds[model]->m_pWorldBsp;
        if (bsp->m_nUnknown48&(4|0x10)) for (uint32 polygon=0;polygon<bsp->m_nParts;++polygon) add({static_cast<uint16>(model),static_cast<uint16>(polygon)});
    }
    std::sort(queue.begin(),queue.end(),[](const Entry& a,const Entry& b) {
        if (a.key>>16!=b.key>>16) return a.key>>16<b.key>>16;
        if (a.polygon->m_pSurface->m_n3e!=b.polygon->m_pSurface->m_n3e) return a.polygon->m_pSurface->m_n3e<b.polygon->m_pSurface->m_n3e;
        return a.key<b.key;
    });
    state.static_polygons.clear(); state.static_polygons.reserve(queue.size());
    Style style; style.record_static=true; state.draw(queue,camera,render,options,stats,style);
    return stats;
}
WorldRenderStats GlWorld::render_brush(const CObjWorldModel& object,const CameraView& camera,ClientTextures::Render& render,WorldRenderOptions options,bool static_already_drawn) {
    return draw_brush(object,camera,render,options,static_already_drawn,false);
}
WorldRenderStats GlWorld::render_sky_brush(const CObjWorldModel& object,const CameraView& camera,ClientTextures::Render& render,WorldRenderOptions options) {
    options.lightmaps=false; options.draw_all=true; return draw_brush(object,camera,render,options,false,true);
}
WorldRenderStats GlWorld::draw_brush(const CObjWorldModel& object,const CameraView& camera,ClientTextures::Render& render,WorldRenderOptions options,bool static_already_drawn,bool sky) {
    auto& state=*state_; state.live();
    if ((object.m_ObjectType!=OT_WORLDMODEL && object.m_ObjectType!=OT_CONTAINER) || !finite(options.light_scale) || !std::isfinite(options.time) || options.time<0 || options.time>1e12)
        throw std::invalid_argument("invalid world model render request");
    const CWorldBsp* model=nullptr; const CWorldSimple* terrain=nullptr; uint32 model_index=0;
    for (uint32 i=0;i<state.world->m_Worlds.GetSize() && !model;++i) {
        const auto* candidate=state.world->m_Worlds[i]->m_pWorldBsp;
        if (object.m_pWorldBsp==candidate) { model=candidate; model_index=i; break; }
        for (const auto& subworld : std::span(candidate->m_Array138.GetArray(),candidate->m_Array138.GetSize()))
            if (object.m_pWorldBsp==&subworld) { model=candidate; terrain=&subworld; model_index=i; break; }
    }
    if (!model) throw std::invalid_argument("world model geometry does not belong to this world");
    for (const auto* matrix : {&object.m_Transform1f0,&object.m_Transform230}) {
        for (unsigned row=0;row<4;++row) for (unsigned column=0;column<4;++column) if (!std::isfinite(matrix->m[row][column])) throw std::invalid_argument("nonfinite world model matrix");
        if (matrix->m[3][0]!=0 || matrix->m[3][1]!=0 || matrix->m[3][2]!=0 || matrix->m[3][3]!=1) throw std::invalid_argument("world model transform is not affine");
    }
    // Sky brushes are compiled in sky-box coordinates and drawn as authored.
    // Their replicated object positions can be compressed into the playable
    // world's bounds, which need not contain the distant sky box at all.
    const auto local_camera=sky ? camera.position() : transform(object.m_Transform230,camera.position());
    std::vector<const CWorldPart*> polygons; WorldRenderStats stats;
    if (terrain) { if (terrain->m_Array50.GetSize()) polygons.assign(terrain->m_Array50.GetArray(),terrain->m_Array50.GetArray()+terrain->m_Array50.GetSize()); }
    else if (model->m_nParts) polygons.assign(model->m_ppParts,model->m_ppParts+model->m_nParts);
    if (polygons.empty()) return stats;
    const auto flags=polygons.front()->m_pSurface->m_nFlags;
    Style style; style.transform=sky ? nullptr : &object.m_Transform1f0; style.color={object.m_ColorR,object.m_ColorG,object.m_ColorB,object.m_ColorA};
    style.chroma=object.m_Flags2&FLAG2_CHROMAKEY;
    style.blend=((flags&0x80000) || (object.m_Flags2&FLAG2_ADDITIVE)) ? ObjectBlend::Additive : object.m_Flags2&FLAG2_MULTIPLY ? ObjectBlend::Multiply :
        ((flags&8) || object.m_ColorA<255) ? ObjectBlend::Alpha : ObjectBlend::Opaque;
    if (sky) { style.sky=true; style.chroma=false; style.blend=(flags&8) ? ((flags&0x80000) ? ObjectBlend::Additive : ObjectBlend::Alpha) : ObjectBlend::Opaque; }
    if (style.blend!=ObjectBlend::Opaque) options.lightmaps=false;
    std::shared_ptr<const WorldVisibility> visibility; WorldVisibility::Selection selection;
    if (!options.draw_all) for (auto* record : std::span(state.world->m_Worlds.GetArray(),state.world->m_Worlds.GetSize())) if (record->m_pWorldBsp->m_nUnknown48&0x20) {
        const auto& bsp=*record->m_pWorldBsp; visibility=bsp.m_Visibility ? bsp.m_Visibility : WorldVisibility::Build(bsp);
        std::vector<uint16> portals(bsp.m_nChunks); for (uint32 i=0;i<bsp.m_nChunks;++i) portals[i]=bsp.m_pChunks[i].m_Flags;
        selection=visibility->Select(camera.position(),portals); break;
    }
    std::unordered_set<const CWorldPart*> allowed(polygons.begin(),polygons.end());
    std::unordered_set<const CWorldPart*> seen;
    std::vector<Entry> queue; queue.reserve(polygons.size());
    auto add=[&](const CWorldPart* polygon) {
        if (!polygon || !allowed.contains(polygon) || !seen.insert(polygon).second) return;
        const auto index=polygon->m_nIndex;
        if (index>=model->m_nParts || model->m_ppParts[index]!=polygon) throw std::invalid_argument("world model polygon index mismatch");
        const uint32 key=model_index<<16|index; ++stats.candidates;
        if (terrain && static_already_drawn && state.static_polygons.contains(key)) return;
        if (polygon->m_pSurface->m_nFlags&4) return;
        if (!sky && (polygon->m_pSurface->m_nFlags&16)) { ++stats.sky; return; }
        const auto center=sky ? polygon->m_vCentre : transform(object.m_Transform1f0,polygon->m_vCentre); double radius=0;
        for (const auto& vertex : polygon->render_vertices(options.fix_tjunctions)) {
            const auto point=sky ? *vertex.m_pPos : transform(object.m_Transform1f0,*vertex.m_pPos);
            radius=std::max(radius,std::hypot(double(point.x)-center.x,double(point.y)-center.y,double(point.z)-center.z));
        }
        const bool backface=(style.blend==ObjectBlend::Opaque || sky) && options.cull && !facing(*polygon,local_camera);
        if (backface ||
            (radius<=std::numeric_limits<float>::max() && (!camera.visible(center,static_cast<float>(radius)) ||
                (visibility && !visibility->Intersects(selection,center,static_cast<float>(radius)))))) { ++stats.culled; return; }
        queue.push_back({key,polygon,model});
    };
    if (style.blend==ObjectBlend::Opaque && !sky) for (auto* polygon : polygons) add(polygon);
    else {
        struct Node { const WorldPolyRef* value; bool polygon; };
        std::vector<Node> pending{{object.m_pWorldBsp->GetRoot(),false}}; std::unordered_set<const WorldPolyRef*> visited;
        const auto maximum=object.m_pWorldBsp->PolygonReferences().size();
        while (!pending.empty()) {
            const auto entry=pending.back(); pending.pop_back(); const auto* node=entry.value;
            if (!node || (node->m_flag&3)) continue;
            if (entry.polygon) { add(node->m_pOwner); continue; }
            if (!visited.insert(node).second) continue;
            if (visited.size()>maximum) throw std::invalid_argument("world model BSP traversal exceeds its nodes");
            const auto* plane=node->m_pOwner->m_pSurface->m_pPlane;
            const unsigned side=double(plane->m_Normal.x)*local_camera.x+double(plane->m_Normal.y)*local_camera.y+double(plane->m_Normal.z)*local_camera.z-plane->m_Dist>=0;
            pending.push_back({node->m_Children[side],false});
            // Sky BSPs use node, far, near order (r_DrawSkyWorldModel),
            // rather than a translucent brush's far, node, near traversal.
            // Their depth writes are disabled, so this order is visible.
            if (sky) { pending.push_back({node->m_Children[!side],false}); pending.push_back({node,true}); }
            else { pending.push_back({node,true}); pending.push_back({node->m_Children[!side],false}); }
        }
        // Disconnected render polygons have no BSP ordering. Keep them
        // visible, using their center distance as the remaining ordering.
        std::vector<const CWorldPart*> remainder;
        for (auto* polygon : polygons) if (!seen.contains(polygon)) remainder.push_back(polygon);
        std::stable_sort(remainder.begin(),remainder.end(),[&](const auto* a,const auto* b) { return a->m_vCentre.DistSqr(local_camera)>b->m_vCentre.DistSqr(local_camera); });
        for (auto* polygon : remainder) add(polygon);
    }
    state.draw(queue,camera,render,options,stats,style); return stats;
}
void GlWorld::State::draw(std::span<const Entry> queue,const CameraView& camera,ClientTextures::Render& render,const WorldRenderOptions& options,WorldRenderStats& stats,const Style& style) {
    auto& state=*this;
    const auto transaction=render.token();
    if (options.lightmaps && state.lighting_render!=transaction) { state.atlas->update(options.lights,options.light_saturate,options.placements); state.lighting_render=transaction; }
    // Vertex-lit polygons take the lights in their BSP's space: a world
    // model's placement is rigid, so its inverse is its transpose.
    std::vector<DynamicWorldLight> local_lights=options.lights;
    if (style.transform) for (auto& light : local_lights) {
        const auto& m=*style.transform; const LTVector d=light.position-LTVector(m.m[0][3],m.m[1][3],m.m[2][3]);
        light.position={m.m[0][0]*d.x+m.m[1][0]*d.y+m.m[2][0]*d.z,m.m[0][1]*d.x+m.m[1][1]*d.y+m.m[2][1]*d.z,m.m[0][2]*d.x+m.m[1][2]*d.y+m.m[2][2]*d.z};
    }
    std::vector<const DynamicWorldLight*> reaching,per_pixel;
    // Only an opaque polygon can take a light as a second, added pass.
    const bool pixel_lights=style.blend==ObjectBlend::Opaque && !style.sky && g_RDCaps.bShaders;
    Frame frame(camera);
    if (style.transform) {
        std::array<float,16> matrix; for (unsigned row=0;row<4;++row) for (unsigned column=0;column<4;++column) matrix[column*4+row]=style.transform->m[row][column];
        rd_MultMatrix(matrix.data());
    }
    rd_DepthMask(style.blend==ObjectBlend::Opaque);
    if (style.blend!=ObjectBlend::Opaque) {
        rd_Enable(RD_BLEND);
        if (style.blend==ObjectBlend::Alpha) rd_BlendFunc(RD_SRC_ALPHA,RD_ONE_MINUS_SRC_ALPHA);
        else if (style.blend==ObjectBlend::Additive) rd_BlendFunc(RD_ONE,RD_ONE);
        else rd_BlendFunc(RD_ZERO,RD_SRC_COLOR);
    }
    if (style.sky) { rd_DepthRange(1,1); rd_DepthMask(false); }
    struct Vertex { LTVector point; float u,v; std::array<float,2> light,detail; std::array<uint8,4> color; };
    // A detail texture or environment map between the base texture and the
    // lightmap, which then takes the third unit.
    const bool three=g_RDCaps.bCombine && g_RDCaps.nTextureUnits>=3 && (options.detail_textures || options.environment_maps);
    const int lightmap_unit=three ? 2 : 1;
    auto eye=camera.position();
    if (style.transform) {
        const auto& m=*style.transform; const LTVector d=eye-LTVector(m.m[0][3],m.m[1][3],m.m[2][3]);
        eye={m.m[0][0]*d.x+m.m[1][0]*d.y+m.m[2][0]*d.z,m.m[0][1]*d.x+m.m[1][1]*d.y+m.m[2][1]*d.z,m.m[0][2]*d.x+m.m[1][2]*d.y+m.m[2][2]*d.z};
    }
    std::vector<Vertex> vertices;
    for (const auto& entry : queue) {
        const auto& polygon=*entry.polygon; const auto& surface=*polygon.m_pSurface;
        const auto source=polygon.render_vertices(options.fix_tjunctions); if (source.size()<3) continue;
        std::string_view filename;
        if (surface.m_n3e<entry.model->m_nc0) filename=static_cast<char* const*>(entry.model->m_pbc)[surface.m_n3e];
        const auto texture=state.material(filename,options.time);
        if (!filename.empty() && !texture) ++stats.missing_materials;
        const auto pixels=texture ? texture->snapshot() : nullptr;
        const auto flags=pixels ? pixels->texture.header.flags : 0;
        const bool fullbright=flags&1;
        // d3d.ren tests alpha only in the chromakey sets: a solid world
        // texture's alpha (window glass, light fixtures) is a gloss mask. The
        // static world's grates and fences were colour keyed instead, and
        // their cleared texels are cut out.
        const bool cutout=!style.sky && (style.chroma || ((flags&0x80) && pixels->keyed));
        const auto location=options.lightmaps && !fullbright && g_RDCaps.nTextureUnits>=2 ? state.atlas->find(entry.key) : std::nullopt;
        const auto scale=pixels ? float(1u<<(pixels->texture.header.extra[4]&15)) : 1.f;
        const float u=pixels ? scale/pixels->texture.mips[0].width() : 0,v=pixels ? scale/pixels->texture.mips[0].height() : 0;
        // A panning-sky receiver takes the cloud shadow where a lightmap
        // would go (r_DrawSPMTPanningSkyPoly).
        std::shared_ptr<const TextureResource> pan; float pan_u=0,pan_v=0;
        if (options.lightmaps && !location && !fullbright && g_RDCaps.nTextureUnits>=2 && (surface.m_nFlags&0x8000) &&
            !options.pan_texture.empty() && options.pan_scale[0]!=0 && options.pan_scale[1]!=0 && (pan=state.material(options.pan_texture,options.time))) {
            const auto pixels_pan=pan->snapshot(); const float pan_texel=float(1u<<(pixels_pan->texture.header.extra[4]&15));
            pan_u=pan_texel/pixels_pan->texture.mips[0].width()/options.pan_scale[0]; pan_v=pan_texel/pixels_pan->texture.mips[0].height()/options.pan_scale[1];
        }
        std::shared_ptr<const TextureResource> second; bool environment=false; float da=0,db=0,dc=0,dd=0;
        if (three && location && texture && pixels) {
            const auto& linked=state.link(*texture,*pixels);
            if (options.detail_textures && linked.type==TextureLinkType::Detail) second=state.material(linked.name,options.time);
            if (!second && options.environment_maps && linked.type==TextureLinkType::Environment && surface.m_pPlane) environment=bool(second=state.material(linked.name,options.time));
            if (second && !environment) {
                // r_DrawOverlayDetail: the base texels rotated and scaled.
                const auto detail=second->snapshot(); const auto& header=detail->texture.header;
                const float detail_scale=float(1u<<(header.extra[4]&15)),detail_u=detail_scale/detail->texture.mips[0].width(),detail_v=detail_scale/detail->texture.mips[0].height();
                const float k=options.detail_scale*pixels->texture.header.detail_scale,angle=pixels->texture.header.detail_angle*3.14159265f/180;
                da=std::cos(angle)*detail_u*k; db=-std::sin(angle)*detail_v*k; dc=std::sin(angle)*detail_u*k; dd=std::cos(angle)*detail_v*k;
            }
        }
        vertices.clear(); vertices.reserve(source.size());
        reaching.clear();
        if (!location && !fullbright && polygon.m_pSurface->m_pPlane) for (const auto& light : local_lights)
            if (light.touches(polygon.m_pSurface->m_pPlane->m_Normal,polygon.m_pSurface->m_pPlane->m_Dist,polygon.m_vCentre,polygon.m_fRadius)) reaching.push_back(&light);
        // A light that only brightens goes on per pixel, as a lightmap takes
        // it: at the vertices alone, a light whose radius flickers across one
        // corner of a large polygon (terrain) lights or unlights all of it.
        per_pixel.clear();
        if (pixel_lights) std::erase_if(reaching,[&](const DynamicWorldLight* light) {
            if (light->solid || light->add[0]<0 || light->add[1]<0 || light->add[2]<0) return false;
            per_pixel.push_back(light); return true;
        });
        for (const auto& vertex : source) {
            if (!vertex.m_pPos || !finite(*vertex.m_pPos) || !std::isfinite(vertex.m_fU) || !std::isfinite(vertex.m_fV)) throw std::invalid_argument("invalid native world render vertex");
            // The lightmap unit doubles under Saturate, so the cloud shadow's
            // receivers take their vertex colours undoubled.
            const bool white=location || fullbright; const double saturation=options.saturate && !location && !fullbright && !pan ? 2 : 1;
            const float tu=vertex.m_fU*u,tv=vertex.m_fV*v;
            if (!std::isfinite(tu) || !std::isfinite(tv)) throw std::invalid_argument("world texture coordinates overflow");
            // r_DrawGouraudDynamicLight: the lights add to the vertex colour.
            std::array<int,3> lit{vertex.m_r,vertex.m_g,vertex.m_b};
            for (const auto* light : reaching) light->apply(*vertex.m_pPos,options.light_saturate,lit);
            for (auto& c : lit) c=std::clamp(c,0,255);
            std::array<float,2> detail{};
            if (second && !environment) detail={vertex.m_fU*da+vertex.m_fV*db,vertex.m_fU*dc+vertex.m_fV*dd};
            else if (environment) {
                // r_CalcEnvMapCoords: the eye's direction reflected about the
                // normal, two components into [0,1], scaled as detail is.
                const auto& normal=surface.m_pPlane->m_Normal; const auto d=*vertex.m_pPos-eye; const auto k=2*d.Dot(normal);
                auto r=d-normal*k; if (const auto length=r.Mag(); length>0) r/=length;
                const auto side=(normal.y*normal.y>.54439f || normal.x*normal.x<normal.z*normal.z) ? r.x : r.z;
                detail={(-.5f*side+.5f)*options.detail_scale,(-.5f*r.y+.5f)*options.detail_scale};
            }
            const auto& at=*vertex.m_pPos;
            vertices.push_back({at,tu,tv,location ? state.atlas->coordinates(entry.key,at) : pan ? std::array<float,2>{(at.x+options.pan_offset[0])*pan_u,(at.z+options.pan_offset[1])*pan_v} : std::array<float,2>{0,0},detail,
                {channel((white ? 255 : lit[0])*double(options.light_scale.x)*saturation*style.color[0]/255),channel((white ? 255 : lit[1])*double(options.light_scale.y)*saturation*style.color[1]/255),channel((white ? 255 : lit[2])*double(options.light_scale.z)*saturation*style.color[2]/255),style.color[3]}});
        }
        rd_ActiveTexture(0);
        if (texture) {
            auto sampling=options.sampling; const auto format=pixels->texture.header.format;
            sampling.alpha=(flags&(0x80|0x100)) || style.chroma || (style.blend!=ObjectBlend::Opaque && (format==TextureFormat::Dxt3 || format==TextureFormat::Dxt5 || format==TextureFormat::IndexedRgba));
            if (style.sky && style.blend==ObjectBlend::Opaque) sampling.alpha=false;
            state.gpu.bind(texture,sampling); render.drawn(texture); rd_Enable(RD_TEXTURE_2D);
        }
        else rd_Disable(RD_TEXTURE_2D);
        rd_TexEnv(RD_ENV_MODULATE);
        if (cutout) { rd_Enable(RD_ALPHA_TEST); rd_AlphaGreater(.1666f); } else rd_Disable(RD_ALPHA_TEST);
        if (three) {
            if (second) {
                auto sampling=options.sampling; sampling.alpha=false;
                state.gpu.bind_unit(1,second,sampling); render.drawn(second); rd_ActiveTexture(1); rd_Enable(RD_TEXTURE_2D);
                rd_TexCombine({options.detail_add ? RD_OP_ADD_SIGNED : RD_OP_MODULATE,RD_ARG_TEXTURE,RD_ARG_PREVIOUS,1,RD_OP_REPLACE,RD_ARG_PREVIOUS,RD_ARG_PREVIOUS});
            } else { rd_ActiveTexture(1); rd_Disable(RD_TEXTURE_2D); }
            rd_ActiveTexture(0);
        }
        if (g_RDCaps.nTextureUnits>=2) {
            rd_ActiveTexture(lightmap_unit);
            if (pan) {
                state.gpu.bind_unit(lightmap_unit,pan,options.sampling); render.drawn(pan); rd_ActiveTexture(lightmap_unit); rd_Enable(RD_TEXTURE_2D);
                if (g_RDCaps.bCombine) rd_TexCombine({RD_OP_MODULATE,RD_ARG_PREVIOUS,RD_ARG_TEXTURE,options.saturate ? 2.f : 1.f,RD_OP_REPLACE,RD_ARG_PREVIOUS,RD_ARG_TEXTURE});
                else rd_TexEnv(RD_ENV_MODULATE);
            } else if (location) {
                state.lightmaps.bind(location->page,options.sampling.linear); rd_Enable(RD_TEXTURE_2D);
                if (g_RDCaps.bCombine) rd_TexCombine({RD_OP_MODULATE,RD_ARG_PREVIOUS,RD_ARG_TEXTURE,options.saturate ? 2.f : 1.f,RD_OP_REPLACE,RD_ARG_PREVIOUS,RD_ARG_TEXTURE});
                else rd_TexEnv(RD_ENV_MODULATE);
            } else rd_Disable(RD_TEXTURE_2D);
            rd_ActiveTexture(0);
        }
        if (style.record_static) state.static_polygons.insert(entry.key);
        rd_Begin(RD_TRIANGLE_FAN);
        for (const auto& vertex : vertices) {
            rd_Color4ubv(vertex.color.data()); rd_TexCoord2f(vertex.u,vertex.v);
            if (second) rd_MultiTexCoord2f(1,vertex.detail[0],vertex.detail[1]);
            if (location || pan) rd_MultiTexCoord2f(lightmap_unit,vertex.light[0],vertex.light[1]);
            rd_Vertex3f(vertex.point.x,vertex.point.y,vertex.point.z);
        }
        rd_End(); ++stats.polygons; stats.triangles+=vertices.size()-2; if (location) ++stats.lightmapped;
        if (!per_pixel.empty() && state.light_program.use()) {
            FixedPipeline fixed; auto& program=state.light_program;
            const double saturation=options.saturate && !pan ? 2 : 1;
            rd_Uniform1i(program.uniform("u_texture"),0); rd_Uniform1i(program.uniform("u_pan"),lightmap_unit);
            rd_Uniform1f(program.uniform("u_textured"),texture ? 1.f : 0.f);
            rd_Uniform1f(program.uniform("u_alpha_test"),cutout ? 1.f : 0.f);
            rd_Uniform1f(program.uniform("u_panned"),pan ? (options.saturate ? 2.f : 1.f) : 0.f);
            rd_Uniform1f(program.uniform("u_saturate"),options.light_saturate); rd_Uniform1f(program.uniform("u_fog"),options.fog ? 1.f : 0.f);
            // The add lands in the vertex colour, which light scale,
            // Saturate and the object's colour then scale, as 0..255 of 255.
            rd_Uniform3f(program.uniform("u_scale"),float(options.light_scale.x*saturation*style.color[0]/(255.*255)),
                float(options.light_scale.y*saturation*style.color[1]/(255.*255)),float(options.light_scale.z*saturation*style.color[2]/(255.*255)));
            rd_Enable(RD_BLEND); rd_BlendFunc(RD_ONE,RD_ONE); rd_DepthMask(false);
            for (const auto* light : per_pixel) {
                rd_Uniform4f(program.uniform("u_light"),light->position.x,light->position.y,light->position.z,light->radius);
                rd_Uniform3f(program.uniform("u_add"),light->add[0],light->add[1],light->add[2]);
                rd_Begin(RD_TRIANGLE_FAN);
                for (const auto& vertex : vertices) {
                    rd_TexCoord2f(vertex.u,vertex.v); if (pan) rd_MultiTexCoord2f(1,vertex.light[0],vertex.light[1]);
                    rd_Vertex3f(vertex.point.x,vertex.point.y,vertex.point.z);
                }
                rd_End();
            }
            rd_DepthMask(true); rd_Disable(RD_BLEND);
        }
    }

}

}
