#pragma once
#include "engine/client/client_engine_base.h"
#include "engine/client/client_cursor.h"
#include "engine/client/client_music.h"
#include "engine/client/client_videos.h"
#include "engine/client/gl_surfaces.h"
#include "engine/client/scene_renderer.h"
#include "engine/client/client_session.h"
#include "engine/client/client_transport.h"
#include "engine/client/client_world_sync.h"
#include "engine/client/local_server.h"
#include <set>
#include "platform/desktop.h"

namespace nolf {
// Platform services the client composes; the host supplies real adapters,
// tests can substitute their own.
struct NativeClientServices {
    std::shared_ptr<const Assets> assets;
    std::shared_ptr<const Resources> resources;
    std::shared_ptr<platform::FontProvider> fonts;
    platform::AudioDecoderFactory audio;
    platform::AudioOutputFactory output;
    platform::VideoDecoderFactory video;
    platform::MusicPerformanceFactory music;
    platform::UserFiles writable;
    // The game data and server game module a local game runs.
    std::filesystem::path data,server_module;
};
// The complete native client engine: every SDK binding, the frame loop and
// the services behind it. The desktop and its render device outlive it.
class NativeClient final : public ClientEngineBase {
    struct Private {};
public:
    static std::shared_ptr<NativeClient> create(platform::Desktop&,NativeClientServices);
    NativeClient(Private,platform::Desktop&,NativeClientServices,std::shared_ptr<ClientScene>,std::shared_ptr<ClientTextures>);
    ~NativeClient() override;
    // Calls OnEngineInitialized with the current display mode.
    LTRESULT initialize(const ClientGameModule::Shell&);
    // One frame: window events, input, shell updates, audio and video.
    // False once the game asked to shut down or the window closed.
    bool frame(const ClientGameModule::Shell&,std::span<const platform::InputEvent>,float elapsed);
    void terminate(const ClientGameModule::Shell&);
    // The player's autoexec.cfg, or the shipped defaults.cfg on a first run.
    void load_config();
    void save_config();
    // A console statement, as from the command line ("+runworld x").
    void console_command(const std::string&);
    // Writes the next presented frame as a BMP.
    void capture(std::filesystem::path);
    const std::string& shutdown_message() const { return shutdown_message_; }
    RMode mode() const;
    LTRESULT render_mode(const RMode&);

    HMESSAGEWRITE StartMessage(uint8) override;
    LTRESULT EndMessage(HMESSAGEWRITE) override;
    LTRESULT EndMessage2(HMESSAGEWRITE,uint32) override;
    LTRESULT SendToServer(ILTMessage&,uint8,uint32) override;
    LTRESULT StartQuery(char*) override;
    LTRESULT UpdateQuery() override;
    LTRESULT GetQueryResults(NetSession*&) override;
    LTRESULT EndQuery() override;
    LTRESULT GetSConValueFloat(char*,float&) override;
    LTRESULT GetSConValueString(char*,char*,uint32) override;
    float GetServerConVarValueFloat(char*) override;
    char* GetServerConVarValueString(char*) override;
    LTRESULT GetAmbientLight(float&) override;
    LTRESULT SetAmbientLight(float) override;
    LTRESULT GetGlobalLightColor(LTVector&) override;
    LTRESULT SetGlobalLightColor(LTVector) override;
    LTRESULT GetGlobalLightDir(LTVector&) override;
    LTRESULT SetGlobalLightDir(LTVector) override;
    LTRESULT StartVideo(char*,uint32) override;
    LTRESULT StopVideo() override;
    LTRESULT UpdateVideo() override;
    LTRESULT IsVideoPlaying() override;
protected:
    ClientSurfaces& surface_service() override { return *surfaces_; }
    ClientFonts& font_service() override { return *fonts_; }
    ClientInput& input_service() override { return *input_; }
    ClientSounds& sound_service() override { return *sounds_; }
private:
    static std::shared_ptr<NativeClient> self();
    static void shutdown_with_message(char*,...);
    static void debug_out(char*,...);
    void bind();
    void resize(std::uint32_t width,std::uint32_t height);
    void shutdown(std::string message);
    LTRESULT render(HOBJECT camera,std::optional<std::span<HOBJECT const>> objects);
    LTRESULT start_game(const StartGameRequest&);
    void disconnect(bool notify);
    void network(float elapsed);
    void session_event(ClientEvent&);
    void queue_message(uint8 id,ILTMessage&);
    ILTMessage* incoming(std::span<const std::byte>);
    platform::Desktop& desktop_;
    NativeClientServices services_;
    std::uint32_t width_=0,height_=0;
    GlSurfaces display_;
    std::shared_ptr<ClientSurfaces> surfaces_;
    std::shared_ptr<ClientFonts> fonts_;
    std::shared_ptr<ClientInput> input_;
    std::shared_ptr<AudioClips> clips_;
    std::shared_ptr<ClientSounds> sounds_;
    std::unique_ptr<ClientVideos> videos_;
    std::unique_ptr<ClientMusic> music_;
    std::unique_ptr<ClientCursor> cursor_;
    std::unique_ptr<SceneRenderer> renderer_;
    const ClientGameModule::Shell* shell_=nullptr;
    ModelHookFn model_hook_=nullptr;
    void* model_hook_user_=nullptr;
    // The engine's sun: no colour until a game sets one, from normalize(0,-2,-1).
    LTVector light_scale_{1,1,1},light_color_{0,0,0},light_direction_{0,-0.894427191f,-0.447213595f};
    // The sky's cloud shadow (GLOBALPAN_SKYSHADOW): texture, offsets, scales.
    struct SkyPan { std::string texture; float x_offset=0,z_offset=0,x_scale=0,z_scale=0; } sky_pan_;
    float ambient_=0;
    bool in_3d_=false,quit_=false;
    std::string shutdown_message_;
    std::optional<std::filesystem::path> capture_;
    std::unique_ptr<LocalServer> server_;
    std::unique_ptr<ClientTransport> transport_;
    std::unique_ptr<ClientSession> session_;
    std::unique_ptr<ClientWorldSync> sync_;
    std::map<ILTMessage*,uint8> message_ids_;
    std::vector<std::pair<uint8,PacketBytes>> pending_messages_;
    std::map<std::string,std::string,std::less<>> server_values_;
    std::set<unsigned> active_commands_;
    std::optional<std::uint16_t> client_object_;
    int game_mode_=GAMEMODE_NONE;
    bool in_world_=false;
    float network_time_=0;
    bool joined_=false;
    SceneRenderStats last_render_;
};
}
