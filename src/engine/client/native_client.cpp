#include "engine/client/native_client.h"
#include "engine/server/ltobject_internal.h"
#include "engine/portable/string.h"
#include "modern/render/rdev.h"
#include "engine/client/client_message_io.h"
#include "engine/server/message.h"
#include "game/Shared/NetDefs.h"
#include "platform/input.h"
#include "platform/logging.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

int parse_Command(char* command,char** next,char* buffer,char** arguments,int* count);
extern int g_NetDebugLevel;

namespace nolf {
namespace {
template<class F> LTRESULT guarded(F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (const std::exception& error) { platform::log("client",error.what()); return LT_ERROR; }
    catch (...) { return LT_ERROR; }
}
std::string format(const char* text,va_list arguments) {
    if (!text) return {};
    std::array<char,4096> output; const int length=std::vsnprintf(output.data(),output.size(),text,arguments);
    return length<0 ? std::string{} : std::string(output.data(),std::min<std::size_t>(std::size_t(length),output.size()-1));
}
void describe(RMode& mode,std::uint32_t width,std::uint32_t height) {
    mode=RMode{}; mode.m_bHardware=LTTRUE; mode.m_Width=width; mode.m_Height=height; mode.m_BitDepth=32;
    std::snprintf(mode.m_RenderDLL,sizeof mode.m_RenderDLL,"%s","opengl.ren");
    std::snprintf(mode.m_InternalName,sizeof mode.m_InternalName,"%s","native");
    std::snprintf(mode.m_Description,sizeof mode.m_Description,"%s","Native OpenGL");
}
}
std::shared_ptr<NativeClient> NativeClient::self() { return std::static_pointer_cast<NativeClient>(active_engine()); }
void NativeClient::shutdown_with_message(char* text,...) {
    va_list arguments; va_start(arguments,text); auto message=format(text,arguments); va_end(arguments);
    if (const auto client=self()) client->shutdown(std::move(message));
}
void NativeClient::debug_out(char* text,...) {
    va_list arguments; va_start(arguments,text); const auto message=format(text,arguments); va_end(arguments);
    platform::log("client",message);
}
std::shared_ptr<NativeClient> NativeClient::create(platform::Desktop& desktop,NativeClientServices services) {
    if (!services.assets || !services.resources || !services.fonts || !services.audio || !services.output || !services.video || !services.music)
        throw std::invalid_argument("native client platform services are required");
    auto scene=std::make_shared<ClientScene>(services.assets);
    auto textures=std::make_shared<ClientTextures>(services.assets);
    auto client=std::make_shared<NativeClient>(Private{},desktop,std::move(services),std::move(scene),std::move(textures));
    client->bind(); client->validate_sdk(); return client;
}
NativeClient::NativeClient(Private,platform::Desktop& desktop,NativeClientServices services,std::shared_ptr<ClientScene> scene,std::shared_ptr<ClientTextures> textures)
    : ClientEngineBase(scene,services.assets,services.resources,textures,services.writable),desktop_(desktop),services_(std::move(services)),
      width_([&] { int w=0,h=0; desktop.drawable_size(w,h); return std::uint32_t(std::max(w,1)); }()),
      height_([&] { int w=0,h=0; desktop.drawable_size(w,h); return std::uint32_t(std::max(h,1)); }()),
      display_(width_,height_) {
    surfaces_=std::make_shared<ClientSurfaces>(services_.assets,width_,height_,display_);
    fonts_=std::make_shared<ClientFonts>(services_.fonts,surfaces_);
    input_=std::make_shared<ClientInput>(platform::desktop_input_controls());
    clips_=std::make_shared<AudioClips>(services_.assets,services_.audio);
    sounds_=std::make_shared<ClientSounds>(clips_,services_.output);
    videos_=std::make_unique<ClientVideos>(VideoServices{services_.assets,services_.video,services_.audio,services_.output,[this] { return double(GetTime()); }},display_,width_,height_);
    music_=std::make_unique<ClientMusic>(MusicServices{services_.assets,services_.music,services_.output});
    cursor_=std::make_unique<ClientCursor>(services_.resources,desktop_.cursor_service());
    renderer_=std::make_unique<SceneRenderer>(scene_,textures_,services_.assets);
    surfaces_->install(*this);
    m_pSoundMgr=sounds_.get(); m_pVideoMgr=videos_.get(); m_pDirectMusicMgr=music_.get(); m_pCursorLT=cursor_.get();
    if (scene_->viewport(width_,height_)!=LT_OK) throw std::runtime_error("native client viewport rejected");
    ClientSoundEvents events; events.code=scene_->retain();
    events.object=[this](HOBJECT object) -> std::optional<SoundPose> {
        if (!scene_->contains(object)) return {};
        return SoundPose{object->m_Pos,object->m_Rotation};
    };
    events.played=[this](PlaySoundInfo& info,HLTSOUND) { if (shell_) shell_->call([&](IClientShell& shell) { shell.OnPlaySound(&info); }); };
    sounds_->events(std::move(events));
    ClientSceneEvents scene_events; scene_events.code=scene_->retain();
    scene_events.removed=[this](HOBJECT object) {
        if (sync_) sync_->forget(object);
        if (shell_) shell_->call([&](IClientShell& game) { game.OnObjectRemove(object); });
    };
    scene_events.touch=[this](HOBJECT object,const CollisionInfo& info,float force) {
        if (shell_) shell_->call([&](IClientShell& game) { auto copy=info; game.OnTouchNotify(object,&copy,force); });
    };
    scene_events.model_key=[this](HOBJECT object,ArgList& arguments) { if (shell_) shell_->call([&](IClientShell& game) { game.OnModelKey(object,&arguments); }); };
    scene_->events(std::move(scene_events));
}
NativeClient::~NativeClient()=default;
void NativeClient::shutdown(std::string message) {
    if (!message.empty()) { platform::log("client",message); if (shutdown_message_.empty()) shutdown_message_=std::move(message); }
    quit_=true;
}
RMode NativeClient::mode() const { RMode result; describe(result,width_,height_); return result; }
void NativeClient::resize(std::uint32_t width,std::uint32_t height) {
    if (!width || !height || (width==width_ && height==height_)) return;
    rd_DestroyDevice();
    if (!rd_CreateDevice(&desktop_,width,height,desktop_.window_mode()!=platform::WindowMode::Fullscreen,1,1)) throw std::runtime_error("native render device restart failed");
    width_=width; height_=height;
    display_.resize(width,height); surfaces_->resize(width,height); videos_->resize(width,height);
    if (scene_->viewport(width,height)!=LT_OK) throw std::runtime_error("native client viewport rejected");
}
LTRESULT NativeClient::render_mode(const RMode& requested) {
    if (!requested.m_Width || !requested.m_Height || requested.m_Width>16384 || requested.m_Height>16384) return LT_INVALIDPARAMS;
    if (requested.m_Width==width_ && requested.m_Height==height_) return LT_OK;
    desktop_.window_mode(desktop_.window_mode(),int(requested.m_Width),int(requested.m_Height));
    int width=0,height=0; desktop_.drawable_size(width,height);
    resize(std::uint32_t(std::max(width,1)),std::uint32_t(std::max(height,1)));
    return LT_OK;
}
LTRESULT NativeClient::render(HOBJECT camera,std::optional<std::span<HOBJECT const>> objects) {
    if (!in_3d_) return LT_NOTIN3D;
    if (!scene_->camera(camera)) return LT_INVALIDPARAMS;
    SceneRenderOptions options; options.models.code=game_code_; options.models.hook=model_hook_; options.models.hook_user=model_hook_user_;
    options.models.time=GetTime();
    // The renderer's console variables, read each frame as the original's were.
    const auto variable=[this](const char* name,float fallback) {
        std::string key=name; const auto handle=GetConsoleVar(key.data());
        const auto value=handle ? GetVarValueFloat(handle) : fallback; return std::isfinite(value) ? value : fallback;
    };
    const bool saturate=variable("Saturate",0)!=0;
    options.world.saturate=saturate;
    options.world.detail_textures=variable("DetailTextures",1)!=0; options.world.detail_add=variable("DetailTextureAdd",1)!=0;
    options.world.detail_scale=variable("DetailTextureScale",.2f); options.world.environment_maps=variable("EnvMapWorld",1)!=0;
    options.world.pan_texture=sky_pan_.texture; options.world.pan_offset={sky_pan_.x_offset,sky_pan_.z_offset}; options.world.pan_scale={sky_pan_.x_scale,sky_pan_.z_scale};
    // Water's environment map adds signed as detail does.
    options.world.environment_add=options.world.detail_add; options.world.polygrid_environment=variable("EnvMapPolyGrids",1)!=0;
    // The global light scale applies to the finished view, not per object.
    options.models.screen_scale=light_scale_; options.models.screen_add=variable("LightAddPoly",1)!=0;
    options.models.saturation=saturate ? std::clamp(variable("ModelSaturation",2),0.f,8.f) : 1.f;
    options.models.far_distance=std::clamp(variable("FarZ",10000),3.f,500000.f);
    options.models.max_lights=std::size_t(std::clamp(variable("MaxModelLights",4),0.f,float(ModelLighting::max_lights)));
    options.models.dynamic_lights=variable("DynamicLight",1)!=0;
    options.filter={variable("Bilinear",1)!=0,variable("Trilinear",0)!=0,std::clamp(variable("Anisotropic",0),1.f,16.f)};
    options.models.sun_direction=light_direction_; options.models.sun_color=light_color_;
    options.models.apply_sun=variable("ModelApplySun",1)!=0; options.models.sun_variance=std::clamp(variable("ModelSunVariance",.05f),0.f,1.f);
    options.fast_lights=variable("FastLight",0)!=0; options.world.light_saturate=std::clamp(variable("LightSaturate",1),0.f,63.f);
    options.models.lod_offset=int(std::clamp(variable("ModelLODOffset",0),-16.f,16.f));
    auto& fog=options.models.fog;
    fog.enabled=variable("FogEnable",0)!=0; fog.sky=variable("SkyFogEnable",1)!=0;
    fog.color={std::clamp(variable("FogR",255),0.f,255.f),std::clamp(variable("FogG",255),0.f,255.f),std::clamp(variable("FogB",255),0.f,255.f)};
    fog.near_z=variable("FogNearZ",0); fog.far_z=variable("FogFarZ",2000);
    fog.sky_near=variable("SkyFogNearZ",0); fog.sky_far=variable("SkyFogFarZ",2000);
    options.models.shadows={int(variable("MaxModelShadows",1)),variable("ModelShadowAlpha",230),variable("ModelShadowOffset",.05f)};
    // Enhanced lighting is opt-in (EnhancedLighting); without it the game's
    // detailed shadows are the original's projected ones (ModelShadowProj).
    using ShadowMapMode=ModelRenderOptions::ShadowMapMode;
    const bool enhanced=variable("EnhancedLighting",0)!=0;
    const bool projected=options.models.shadows.max>0 && variable("ModelShadowProj",0)!=0;
    options.models.shadow_maps=enhanced ? ShadowMapMode::Enhanced : projected ? ShadowMapMode::Projected : ShadowMapMode::Off;
    options.models.shadow_resolution=int(enhanced ? variable("ShadowMapRes",512) : variable("ModelShadowProjRes",128));
    options.models.shadow_casters=int(enhanced ? variable("MaxShadowCasters",16) : 8);
    options.models.shadow_lights=int(enhanced ? variable("ShadowLightsPerCaster",2) : 1);
    options.models.chrome=variable("EnvMapEnable",0)!=0; options.models.chrome_all=variable("EnvMapAll",0)!=0;
    options.models.environment_scale=variable("EnvScale",1); options.models.environment_pan=variable("EnvPanSpeed",.0005f);
    { char name[]="EnvMap"; const auto handle=GetConsoleVar(name); const char* map=handle ? GetVarValueString(handle) : nullptr; options.models.environment_map=map ? map : ""; }
    last_render_=objects ? renderer_->render_objects(camera,*objects,options) : renderer_->render(camera,options);
    return LT_OK;
}
LTRESULT NativeClient::initialize(const ClientGameModule::Shell& shell) {
    const auto scope=activate(); shell_=&shell;
    RMode current=mode(); LTGUID guid{};
    const auto result=shell.call([&](IClientShell& game) { return game.OnEngineInitialized(&current,&guid); });
    if (result!=LT_OK) shutdown("The game failed to initialize.");
    return result;
}
void NativeClient::load_config() {
    // autoexec.cfg keeps only saved (+) variables, so the shipped defaults
    // come first and the player's settings override them.
    const auto scope=activate(); char user[]="autoexec.cfg",shipped[]="defaults.cfg";
    ReadConfigFile(shipped);
    if (writable_.file_exists(user)) ReadConfigFile(user);
}
void NativeClient::save_config() {
    const auto scope=activate(); char name[]="autoexec.cfg";
    if (WriteConfigFile(name)!=LT_OK) platform::log("client","cannot save autoexec.cfg");
}
void NativeClient::console_command(const std::string& command) {
    const auto scope=activate(); std::string text=command; RunConsoleString(text.data());
}
void NativeClient::capture(std::filesystem::path path) { require_owner(); capture_=std::move(path); }
void NativeClient::terminate(const ClientGameModule::Shell& shell) {
    const auto scope=activate();
    disconnect(true);
    shell.call([](IClientShell& game) { game.OnEngineTerm(); });
    music_->Term(); shell_=nullptr;
}
bool NativeClient::frame(const ClientGameModule::Shell& shell,std::span<const platform::InputEvent> events,float elapsed) {
    const auto scope=activate(); shell_=&shell;
    for (const auto& event : events) {
        if (event.type==platform::InputType::quit) { shutdown({}); continue; }
        if (event.type==platform::InputType::resize) { int width=0,height=0; desktop_.drawable_size(width,height); resize(std::uint32_t(std::max(width,1)),std::uint32_t(std::max(height,1))); }
        shell.input(event);
        if (event.type==platform::InputType::key) {
            const auto key=int(input_->logical_key(unsigned(event.code)));
            if (key) shell.call([&](IClientShell& game) { if (event.pressed) game.OnKeyDown(key,event.repeat ? 1 : 0); else game.OnKeyUp(key); });
        }
    }
    const auto commands=input_frame(events,desktop_.gamepad_state());
    shell.call([&](IClientShell& game) {
        for (const auto command : commands.pressed) game.OnCommandOn(int(command));
        for (const auto command : commands.released) game.OnCommandOff(int(command));
    });
    for (const auto command : commands.pressed) active_commands_.insert(command);
    for (const auto command : commands.released) active_commands_.erase(command);
    begin_frame(std::clamp(elapsed,0.f,1.f));
    network(elapsed);
    scene_->update(elapsed);
    shell.call([](IClientShell& game) { game.PreUpdate(); game.Update(); game.PostUpdate(); });
    finish_frame();
    // The listener follows the client object, at the origin without one
    // (clientsoundmgr.cpp 0x00480dce).
    SoundPose listener; if (const auto object=GetClientObject()) listener={object->m_Pos,object->m_Rotation};
    sounds_->update(elapsed,listener); sounds_->pump(); music_->pump(); videos_->pump();
    return !quit_;
}
void NativeClient::bind() {
    StartGame=+[](StartGameRequest* request) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        return request ? guarded([&] { return client->start_game(*request); }) : LT_INVALIDPARAMS;
    };
    GetGameMode=+[](int* mode) -> LTRESULT { if (!mode) return LT_INVALIDPARAMS; const auto client=self(); *mode=client ? client->game_mode_ : GAMEMODE_NONE; return client ? LT_OK : LT_NOTINITIALIZED; };
    GetLocalClientID=+[](uint32* id) -> LTRESULT {
        if (id) *id=0;
        const auto client=self(); if (!client || !client->session_ || !client->session_->client_id()) return LT_NOTCONNECTED;
        if (id) *id=*client->session_->client_id();
        return LT_OK;
    };
    IsConnected=+[]() -> LTBOOL { const auto client=self(); return client && client->transport_ && client->transport_->connected() ? LTTRUE : LTFALSE; };
    Disconnect=+[]() { if (const auto client=self()) client->disconnect(true); };
    Shutdown=+[]() { if (const auto client=self()) client->shutdown({}); };
    ShutdownWithMessage=shutdown_with_message;
    FlipScreen=+[](uint32) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        return guarded([&] {
            if (client->capture_) {
                const auto path=std::move(*client->capture_); client->capture_.reset();
                const auto image=client->display_.read({0,0,int(client->width_),int(client->height_)});
                platform::atomic_write(path,encode_bmp(image));
            }
            rd_Present(); return LT_OK;
        });
    };
    ClearScreen=+[](LTRect* rectangle,uint32 flags) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        return guarded([&]() -> LTRESULT {
            if (!flags) flags=CLEARSCREEN_SCREEN|CLEARSCREEN_RENDER;
            std::optional<PixelRect> area; if (rectangle) area=PixelRect{rectangle->left,rectangle->top,rectangle->right,rectangle->bottom};
            if (flags&CLEARSCREEN_SCREEN) client->display_.clear(area,0);
            if (flags&CLEARSCREEN_RENDER) rd_Clear(RD_CLEAR_DEPTH);
            return LT_OK;
        });
    };
    Start3D=+[]() -> LTRESULT { const auto client=self(); if (!client) return LT_NOTINITIALIZED; if (client->in_3d_) return LT_ALREADYIN3D; client->in_3d_=true; return LT_OK; };
    End3D=+[]() -> LTRESULT { const auto client=self(); if (!client) return LT_NOTINITIALIZED; if (!client->in_3d_) return LT_NOTIN3D; client->in_3d_=false; return LT_OK; };
    RenderCamera=+[](HLOCALOBJ camera) -> LTRESULT { const auto client=self(); return client ? guarded([&] { return client->render(camera,std::nullopt); }) : LT_NOTINITIALIZED; };
    RenderObjects=+[](HLOCALOBJ camera,HLOCALOBJ* objects,int count) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        if (count<0 || (count && !objects)) return LT_INVALIDPARAMS;
        return guarded([&] { return client->render(camera,std::span<HOBJECT const>(objects,std::size_t(count))); });
    };
    GetRenderModes=+[]() -> RMode* {
        const auto client=self(); if (!client) return nullptr;
        RMode* head=nullptr;
        try {
            std::vector<std::pair<int,int>> sizes{{int(client->width_),int(client->height_)}};
            for (const auto& mode : client->desktop_.display_modes()) sizes.emplace_back(mode.width,mode.height);
            std::sort(sizes.begin(),sizes.end()); sizes.erase(std::unique(sizes.begin(),sizes.end()),sizes.end());
            for (auto it=sizes.rbegin();it!=sizes.rend();++it) { auto* mode=new RMode; describe(*mode,std::uint32_t(it->first),std::uint32_t(it->second)); mode->m_pNext=head; head=mode; }
        } catch (...) { while (head) { auto* next=head->m_pNext; delete head; head=next; } }
        return head;
    };
    RelinquishRenderModes=+[](RMode* modes) { while (modes) { auto* next=modes->m_pNext; delete modes; modes=next; } };
    GetRenderMode=+[](RMode* mode) -> LTRESULT { const auto client=self(); if (!client) return LT_NOTINITIALIZED; if (!mode) return LT_INVALIDPARAMS; *mode=client->mode(); return LT_OK; };
    SetRenderMode=+[](RMode* mode) -> LTRESULT { const auto client=self(); if (!client) return LT_NOTINITIALIZED; return mode ? guarded([&] { return client->render_mode(*mode); }) : LT_INVALIDPARAMS; };
    ShutdownRender=+[](uint32) -> LTRESULT { return self() ? LT_OK : LT_NOTINITIALIZED; };
    PlayJoystickEffect=+[](char*,float,float) {};
    // The engine's IMA/DirectMusic song API predates ILTDirectMusicMgr, which
    // the game uses instead.
    InitMusic=+[](char*) -> LTBOOL { return LTFALSE; };
    SetMusicDirectory=+[](char*) -> LTBOOL { return LTFALSE; };
    InitInstruments=+[](char*,char*) -> LTBOOL { return LTFALSE; };
    LoadSong=+[](char*) -> LTBOOL { return LTFALSE; };
    DestroyAllSongs=+[]() {};
    PlayList=+[](char*,char*,LTBOOL,uint32) -> LTBOOL { return LTFALSE; };
    PlayBreak=+[](char*,uint32) -> LTBOOL { return LTFALSE; };
    StopMusic=+[](uint32) {};
    PauseMusic=+[]() -> LTBOOL { return LTFALSE; };
    ResumeMusic=+[]() -> LTBOOL { return LTFALSE; };
    AddSongToPlayList=+[](char*,char*) -> LTBOOL { return LTFALSE; };
    DeletePlayList=+[](char*) {};
    PlayMotif=+[](char*,LTBOOL) -> LTBOOL { return LTFALSE; };
    StopMotif=+[](char*) {};
    GetMusicVolume=+[]() -> short { return 0; };
    SetMusicVolume=+[](short) {};
    CastRay=+[](ClientIntersectQuery* query,ClientIntersectInfo* info) -> LTBOOL {
        const auto client=self(); if (!client || !query || !info || !client->IntersectSegment) return LTFALSE;
        const auto length=query->m_Direction.Mag(); if (!(length>0) || !std::isfinite(length)) return LTFALSE;
        auto ray=*query; ray.m_To=query->m_From+query->m_Direction*(100000.f/length);
        return client->IntersectSegment(&ray,info);
    };
    FindObjectsInSphere=+[](LTVector* center,float radius,HLOCALOBJ* objects,uint32 capacity,uint32* written,uint32* found) -> LTRESULT {
        if (written) *written=0;
        if (found) *found=0;
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        if (!center || !written || !found || (capacity && !objects) || !(radius>=0)) return LT_INVALIDPARAMS;
        return guarded([&]() -> LTRESULT {
            for (const auto& entry : client->scene_->visible(*center,radius)) {
                auto* object=entry.object; if (!client->scene_->contains(object) || (object->m_Pos-*center).MagSqr()>radius*radius) continue;
                if (*written<capacity) objects[(*written)++]=object;
                ++*found;
            }
            return LT_OK;
        });
    };
    // Only the sky's shadow pan reaches the renderer.
    SetGlobalPanTexture=+[](uint32 index,char* name) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        if (index==GLOBALPAN_SKYSHADOW) client->sky_pan_.texture=name ? name : ""; return LT_OK;
    };
    SetGlobalPanInfo=+[](uint32 index,float x_offset,float z_offset,float x_scale,float z_scale) -> LTRESULT {
        const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        if (!std::isfinite(x_offset) || !std::isfinite(z_offset) || !std::isfinite(x_scale) || !std::isfinite(z_scale)) return LT_INVALIDPARAMS;
        if (index==GLOBALPAN_SKYSHADOW) { auto& pan=client->sky_pan_; pan.x_offset=x_offset; pan.z_offset=z_offset; pan.x_scale=x_scale; pan.z_scale=z_scale; }
        return LT_OK;
    };
    AddSurfaceEffect=+[](SurfaceEffectDesc*) -> LTRESULT { return self() ? LT_OK : LT_NOTINITIALIZED; };
    GetGameTime=+[]() -> float { const auto client=self(); return client && client->sync_ ? client->sync_->server_time() : 0.f; };
    GetGameFrameTime=+[]() -> float { const auto client=self(); return client ? client->GetFrameTime() : 0.f; };
    DebugOut=debug_out;
    GetClientObject=+[]() -> HLOCALOBJ { const auto client=self(); return client && client->sync_ && client->client_object_ ? client->sync_->object(*client->client_object_) : nullptr; };
    GetGlobalLightScale=+[](LTVector* scale) { if (const auto client=self(); client && scale) *scale=client->light_scale_; };
    SetGlobalLightScale=+[](LTVector* scale) {
        const auto client=self(); if (!client || !scale) return;
        for (int i=0;i<3;++i) client->light_scale_[i]=std::isfinite((*scale)[i]) ? std::clamp((*scale)[i],0.f,1.f) : 1.f;
    };
    OffsetGlobalLightScale=+[](LTVector* offset) {
        const auto client=self(); if (!client || !offset) return;
        for (int i=0;i<3;++i) if (std::isfinite((*offset)[i])) client->light_scale_[i]=std::clamp(client->light_scale_[i]+(*offset)[i],0.f,1.f);
    };
    Parse=+[](char* command,char** next,char* buffer,char** arguments,int* count) -> int {
        if (!command || !next || !buffer || !arguments || !count) return 0;
        return parse_Command(command,next,buffer,arguments,count);
    };
    GetLightColor=+[](HLOCALOBJ object,float* r,float* g,float* b) {
        if (r) *r=0;
        if (g) *g=0;
        if (b) *b=0;
        const auto client=self(); if (!client || !client->scene_->contains(object) || object->m_ObjectType!=OT_LIGHT) return;
        if (r) *r=object->m_ColorR/255.f;
        if (g) *g=object->m_ColorG/255.f;
        if (b) *b=object->m_ColorB/255.f;
    };
    SetLightColor=+[](HLOCALOBJ object,float r,float g,float b) {
        const auto client=self(); if (!client || !client->scene_->contains(object) || object->m_ObjectType!=OT_LIGHT) return;
        const auto byte=[](float value) { return uint8(std::lround(std::clamp(std::isfinite(value) ? value : 0.f,0.f,1.f)*255)); };
        object->m_ColorR=byte(r); object->m_ColorG=byte(g); object->m_ColorB=byte(b);
    };
    GetLightRadius=+[](HLOCALOBJ object) -> float {
        const auto client=self(); return client && client->scene_->contains(object) && object->m_ObjectType==OT_LIGHT ? object->m_LightRadius : 0.f;
    };
    SetLightRadius=+[](HLOCALOBJ object,float radius) {
        const auto client=self(); if (!client || !client->scene_->contains(object) || object->m_ObjectType!=OT_LIGHT || !std::isfinite(radius)) return;
        object->m_LightRadius=std::clamp(radius,0.f,1e6f);
    };
    GetSurfaceBounds=+[](SurfaceData*,LTVector* minimum,LTVector* maximum) -> LTBOOL { if (minimum) minimum->Init(); if (maximum) maximum->Init(); return LTFALSE; };
    SetPolyHideStatus=+[](HPOLY,LTBOOL) -> LTRESULT { return self() ? LT_UNSUPPORTED : LT_NOTINITIALIZED; };
    GetPolyTextureFlags=+[](HPOLY polygon,uint32* flags) -> LTRESULT { const auto client=self(); return client ? client->scene_->common().GetPolyTextureFlags(polygon,flags) : LT_NOTINITIALIZED; };
    SetModelHook=+[](ModelHookFn hook,void* user) -> LTRESULT { const auto client=self(); if (!client) return LT_NOTINITIALIZED; client->model_hook_=hook; client->model_hook_user_=user; return LT_OK; };
    GetEngineHook=+[](char* name,void** data) -> LTRESULT {
        if (!data) return LT_INVALIDPARAMS;
        *data=nullptr; const auto client=self(); if (!client) return LT_NOTINITIALIZED;
        if (name && ascii_compare(name,"NOLFWindow")==0) { *data=static_cast<platform::Window*>(&client->desktop_); return LT_OK; }
        return LT_NOTFOUND;
    };
    InitNetworking=+[](char*,uint32) -> LTRESULT { return self() ? LT_OK : LT_NOTINITIALIZED; };
    GetServiceList=+[](NetService*& list) -> LTRESULT { list=nullptr; return self() ? LT_UNSUPPORTED : LT_NOTINITIALIZED; };
    FreeServiceList=+[](NetService*) -> LTRESULT { return LT_OK; };
    SelectService=+[](HNETSERVICE) -> LTRESULT { return LT_UNSUPPORTED; };
    GetSessionList=+[](NetSession*& list,char*) -> LTRESULT { list=nullptr; return LT_UNSUPPORTED; };
    FreeSessionList=+[](NetSession*) -> LTRESULT { return LT_OK; };
    AddInternetDriver=+[]() -> LTRESULT { return LT_UNSUPPORTED; };
    RemoveInternetDriver=+[]() -> LTRESULT { return LT_OK; };
    IsLobbyLaunched=+[](char*) -> LTRESULT { return LT_NO; };
    GetLobbyLaunchInfo=+[](char*,void** data) -> LTRESULT { if (data) *data=nullptr; return LT_NOTFOUND; };
    GetTcpIpAddress=+[](char* address,uint32 capacity) -> LTRESULT { if (address && capacity) address[0]='\0'; return LT_NOTFOUND; };
}
HMESSAGEWRITE NativeClient::StartMessage(uint8 id) {
    require_owner(); auto* message=StartHMessageWrite();
    if (message) message_ids_[message]=id;
    return message;
}
LTRESULT NativeClient::EndMessage(HMESSAGEWRITE message) { return EndMessage2(message,MESSAGE_GUARANTEED); }
LTRESULT NativeClient::EndMessage2(HMESSAGEWRITE message,uint32) {
    require_owner(); if (!message) return LT_INVALIDPARAMS;
    const auto found=message_ids_.find(message);
    if (found==message_ids_.end()) return LT_INVALIDPARAMS;
    const auto id=found->second; message_ids_.erase(found);
    struct Release { ILTMessage* message; ~Release() { message->Release(); } } owner{message};
    if (!session_) return LT_NOTCONNECTED;
    return guarded([&] { queue_message(id,*message); return LT_OK; });
}
LTRESULT NativeClient::SendToServer(ILTMessage& message,uint8 id,uint32) {
    require_owner(); if (!session_) return LT_NOTCONNECTED;
    return guarded([&] { queue_message(id,message); return LT_OK; });
}
void NativeClient::queue_message(uint8 id,ILTMessage& message) {
    auto& native=static_cast<LMessageImpl&>(message);
    if (!native.m_pOwner) throw std::invalid_argument("foreign client message");
    const auto* data=reinterpret_cast<const std::byte*>(native.m_pOwner->m_pData);
    const auto length=native.m_pOwner->m_DataLen;
    PacketBytes payload; if (length>1) payload.assign(data+1,data+length);
    if (pending_messages_.size()>=4096) throw std::length_error("too many unsent client messages");
    pending_messages_.emplace_back(id,std::move(payload));
}
ILTMessage* NativeClient::incoming(std::span<const std::byte> bytes) {
    ILTMessage* created=nullptr;
    if (scene_->common().CreateMessage(created)!=LT_OK || !created) throw std::bad_alloc();
    auto& native=*static_cast<LMessageImpl*>(created);
    native.SetObjectIO(client_message_io(scene_,resources_,shared_from_this()));
    if (!bytes.empty() && native.WriteRaw(const_cast<std::byte*>(bytes.data()),uint32(bytes.size()))!=LT_OK) { created->Release(); throw std::bad_alloc(); }
    native.ResetPos(); return created;
}
LTRESULT NativeClient::start_game(const StartGameRequest& request) {
    if (request.m_Type!=STARTGAME_NORMAL && request.m_Type!=STARTGAME_HOST && request.m_Type!=STARTGAME_HOSTTCP && request.m_Type!=STARTGAME_CLIENTTCP) return LT_UNSUPPORTED;
    if (request.m_RecordFilename[0] || request.m_PlaybackFilename[0]) return LT_UNSUPPORTED;
    disconnect(true);
    std::string address;
    if (request.m_Type==STARTGAME_CLIENTTCP) {
        address.assign(request.m_TCPAddress,strnlen(request.m_TCPAddress,sizeof request.m_TCPAddress));
        if (address.empty()) return LT_INVALIDPARAMS;
    } else {
        if (services_.data.empty() || services_.server_module.empty()) return LT_UNSUPPORTED;
        std::span<const std::byte> info;
        if (request.m_pGameInfo && request.m_GameInfoLen) info={static_cast<const std::byte*>(request.m_pGameInfo),request.m_GameInfoLen};
        server_=std::make_unique<LocalServer>(services_.data,services_.server_module,resources_,info);
        address=server_->address();
    }
    try {
        if (std::getenv("NOLF_NET_TRACE")) g_NetDebugLevel=2;
        transport_=std::make_unique<ClientTransport>(kNolfApplicationGuid);
        const auto joined=server_ ? transport_->connect_local(server_->driver()) : transport_->connect(address);
        if (joined!=LT_OK) throw std::runtime_error("cannot reach the game server at "+address);
        session_=std::make_unique<ClientSession>(services_.assets);
        std::span<const std::byte> data;
        if (request.m_pClientData && request.m_ClientDataLen) data={static_cast<const std::byte*>(request.m_pClientData),request.m_ClientDataLen};
        session_->begin(data);
        ClientSyncEvents events; events.code=game_code_;
        events.special_effect=[this](HOBJECT object,ILTMessage& message) { if (shell_) shell_->call([&](IClientShell& game) { game.SpecialEffectNotify(object,&message); }); };
        events.move=[this](HOBJECT object,bool teleport,LTVector& place) { if (shell_) shell_->call([&](IClientShell& game) { game.OnObjectMove(object,teleport,&place); }); };
        events.rotate=[this](HOBJECT object,bool teleport,LTRotation& turn) { if (shell_) shell_->call([&](IClientShell& game) { game.OnObjectRotate(object,teleport,&turn); }); };
        events.sound=[this](PlaySoundInfo& info,const std::string& file,float) -> HLTSOUND {
            std::snprintf(info.m_szSoundName,sizeof info.m_szSoundName,"%s",file.c_str()); HLTSOUND handle=nullptr;
            return sounds_->PlaySound(&info,handle)==LT_OK ? handle : nullptr;
        };
        events.move_sound=[this](HLTSOUND sound,const LTVector& position) { auto place=position; sounds_->SetSoundPosition(sound,&place); };
        events.stop_sound=[this](HLTSOUND sound) { sounds_->KillSound(sound); };
        events.global_light=[this](LTVector direction,LTVector color,float scale) {
            SetGlobalLightDir(direction); SetGlobalLightColor(color); LTVector all(scale,scale,scale); SetGlobalLightScale(&all);
        };
        sync_=std::make_unique<ClientWorldSync>(scene_,resources_,shared_from_this(),std::move(events));
    } catch (...) { disconnect(false); throw; }
    game_mode_=request.m_Type==STARTGAME_CLIENTTCP ? STARTGAME_CLIENTTCP : request.m_Type; network_time_=0; joined_=false;
    return LT_OK;
}
void NativeClient::disconnect(bool notify) {
    if (in_world_ && notify && shell_) shell_->call([](IClientShell& game) { game.OnExitWorld(); });
    in_world_=false;
    if (transport_) transport_->disconnect();
    transport_.reset(); session_.reset(); sync_.reset(); server_.reset();
    pending_messages_.clear(); server_values_.clear(); client_object_.reset(); game_mode_=GAMEMODE_NONE;
    if (!scene_->world_name().empty()) scene_->unload_world();
}
void NativeClient::network(float elapsed) {
    if (!session_) return;
    try {
        if (server_) server_->update();
        network_time_+=elapsed;
        const auto flush=[&] { if (transport_->connected()) for (auto& packet : session_->take_outgoing()) transport_->send(packet); };
        flush();
        for (auto& packet : transport_->pump(network_time_)) session_->receive(packet);
        if (transport_->connected()) joined_=true;
        for (auto& event : session_->take_events()) { session_event(event); if (!session_) return; }
        const auto phase=session_->phase();
        if (session_->version_verified() && session_->client_id()) {
            for (auto& [id,payload] : std::exchange(pending_messages_,{})) session_->send_game(id,payload);
            if (phase==ClientSession::Phase::InWorld) {
                std::vector<std::uint8_t> active; for (const auto command : active_commands_) if (command<255) active.push_back(std::uint8_t(command));
                char rate[]="UpdateRate"; const auto variable=GetConsoleVar(rate);
                const auto requested=variable ? GetVarValueFloat(variable) : 0.f;
                session_->send_commands(std::uint8_t(std::clamp(requested>0 ? requested : 30.f,1.f,60.f)),active);
            }
        }
        flush();
        if (g_NetDebugLevel>=2 && sync_ && int(network_time_)!=int(network_time_-elapsed))
            platform::log("client","mirrored objects "+std::to_string(sync_->size())+", client object "+(GetClientObject() ? "present" : "absent")+
                "; last render: "+std::to_string(last_render_.models.models)+" models, "+std::to_string(last_render_.models.culled)+" culled, "+
                std::to_string(last_render_.sprites.sprites)+" sprites, "+std::to_string(last_render_.brushes.objects)+" brushes");
        if (g_NetDebugLevel>=2 && int(network_time_)!=int(network_time_-elapsed))
            for (const auto& entry : scene_->snapshot()) {
                auto* object=entry.object;
                if (!scene_->contains(object) || !(object->m_Flags&FLAG_REALLYCLOSE)) continue;
                const auto info=scene_->info(object);
                platform::log("client","close object type "+std::to_string(object->m_ObjectType)+" flags "+std::to_string(object->m_Flags)+" at "+
                    std::to_string(object->m_Pos.x)+","+std::to_string(object->m_Pos.y)+","+std::to_string(object->m_Pos.z)+" scale "+std::to_string(object->m_Scale.x)+
                    " file "+(info ? info->filename : std::string("?")));
            }
        if (joined_ && !transport_->connected()) throw std::runtime_error("the connection to the game server was lost");
        if (!joined_ && network_time_>10) throw std::runtime_error("the game server did not accept the connection");
    } catch (const std::exception& error) {
        platform::log("client",error.what());
        disconnect(true);
        if (shell_) shell_->call([&](IClientShell& game) { game.SetDisconnectCode(0,error.what()); });
    }
}
void NativeClient::session_event(ClientEvent& event) {
    using Kind=ClientEvent::Kind;
    if (g_NetDebugLevel>=2) platform::log("client","session event "+std::to_string(int(event.kind))+" id "+std::to_string(event.id)+" bytes "+std::to_string(event.bytes.size()));
    switch (event.kind) {
    case Kind::UnloadWorld:
        if (in_world_ && shell_) shell_->call([](IClientShell& game) { game.OnExitWorld(); });
        in_world_=false; client_object_.reset(); if (sync_) sync_->reset();
        if (!scene_->world_name().empty()) scene_->unload_world();
        break;
    case Kind::LoadWorld: {
        std::string name=event.name; platform::log("client","loading world "+name);
        if (shell_) shell_->call([&](IClientShell& game) { game.PreLoadWorld(name.data()); });
        if (sync_) sync_->reset();
        const auto loaded=scene_->load_world(name);
        if (loaded!=LT_OK) throw std::runtime_error("cannot load world "+name);
        // As the original: the last step of loading a world, before the
        // server sends the player its state.
        in_world_=true; platform::log("client","entered world");
        if (shell_) shell_->call([](IClientShell& game) { game.OnEnterWorld(); });
        session_->world_loaded();
        break;
    }
    case Kind::PreloadEnd: platform::log("client","preload complete"); session_->preload_complete(); break;
    case Kind::ClientObject:
        if (event.object_id==0xffff) client_object_.reset();
        else {
            client_object_=event.object_id;
            if (g_NetDebugLevel>=2) platform::log("client","client object "+std::to_string(event.object_id)+(sync_ && sync_->object(event.object_id) ? " present" : " not yet created"));

        }
        break;
    case Kind::GameMessage: {
        auto* message=incoming(event.bytes);
        struct Release { ILTMessage* message; ~Release() { message->Release(); } } owner{message};
        if (shell_) shell_->call([&](IClientShell& game) { game.OnMessage(event.id,message); });
        break;
    }
    case Kind::Identity: platform::log("client","connected as client "+std::to_string(event.object_id)); break;
    case Kind::ConsoleValue: server_values_[event.name]=event.value; break;
    case Kind::Sky: if (event.sky) scene_->sky(*event.sky); break;
    case Kind::EnginePacket: if (sync_) sync_->apply(event.id,event.bytes,session_->files()); break;
    default: break;
    }
}
LTRESULT NativeClient::StartQuery(char*) { return LT_UNSUPPORTED; }
LTRESULT NativeClient::UpdateQuery() { return LT_UNSUPPORTED; }
LTRESULT NativeClient::GetQueryResults(NetSession*& list) { list=nullptr; return LT_UNSUPPORTED; }
LTRESULT NativeClient::EndQuery() { return LT_OK; }
LTRESULT NativeClient::GetSConValueFloat(char* name,float& value) {
    value=0; if (!name) return LT_INVALIDPARAMS;
    const auto found=server_values_.find(std::string_view(name)); if (found==server_values_.end()) return LT_NOTFOUND;
    value=float(std::strtod(found->second.c_str(),nullptr)); return LT_OK;
}
LTRESULT NativeClient::GetSConValueString(char* name,char* buffer,uint32 capacity) {
    if (buffer && capacity) buffer[0]='\0';
    if (!name || !buffer || !capacity) return LT_INVALIDPARAMS;
    const auto found=server_values_.find(std::string_view(name)); if (found==server_values_.end()) return LT_NOTFOUND;
    std::snprintf(buffer,capacity,"%s",found->second.c_str()); return LT_OK;
}
float NativeClient::GetServerConVarValueFloat(char* name) { float value=0; GetSConValueFloat(name,value); return value; }
char* NativeClient::GetServerConVarValueString(char* name) {
    if (!name) return nullptr;
    const auto found=server_values_.find(std::string_view(name));
    return found==server_values_.end() ? nullptr : found->second.data();
}
LTRESULT NativeClient::GetAmbientLight(float& light) { light=ambient_; return LT_OK; }
LTRESULT NativeClient::SetAmbientLight(float light) { if (!std::isfinite(light)) return LT_INVALIDPARAMS; ambient_=std::clamp(light,0.f,1.f); return LT_OK; }
LTRESULT NativeClient::GetGlobalLightColor(LTVector& color) { color=light_color_; return LT_OK; }
LTRESULT NativeClient::SetGlobalLightColor(LTVector color) { light_color_=color; return LT_OK; }
LTRESULT NativeClient::GetGlobalLightDir(LTVector& direction) { direction=light_direction_; return LT_OK; }
LTRESULT NativeClient::SetGlobalLightDir(LTVector direction) { light_direction_=direction; return LT_OK; }
LTRESULT NativeClient::StartVideo(char*,uint32) { return LT_UNSUPPORTED; }
LTRESULT NativeClient::StopVideo() { return LT_UNSUPPORTED; }
LTRESULT NativeClient::UpdateVideo() { return LT_UNSUPPORTED; }
LTRESULT NativeClient::IsVideoPlaying() { return LT_NO; }
}
