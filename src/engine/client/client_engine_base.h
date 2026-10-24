#pragma once
#include "engine/client/client_scene.h"
#include "engine/client/client_textures.h"
#include "engine/client/surface_api.h"
#include "engine/client/client_fonts.h"
#include "engine/client/client_input.h"
#include "engine/client/client_sounds.h"
#include "engine/portable/resources.h"
#include "engine/portable/client_module.h"
#include "platform/user_files.h"
#include "engine/server/console.h"
#include <thread>

class CMsgObjectIO;
namespace nolf {
// Common native SDK implementation. Rendering, input, networking and media
// remain abstract until a concrete host supplies their real implementations.
class ClientEngineBase : public ILTClient, public std::enable_shared_from_this<ClientEngineBase> {
public:
    ~ClientEngineBase() override;
    ClientEngineBase(const ClientEngineBase&)=delete;
    ClientEngineBase& operator=(const ClientEngineBase&)=delete;
    class Scope {
    public:
        explicit Scope(ClientEngineBase&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        std::shared_ptr<ClientEngineBase> engine_,previous_;
        ClientScene::Scope scene_;
        ClientSurfaces::Scope surfaces_;
        ClientFonts::Scope fonts_;
        ClientInput::Scope input_;
        ClientSounds::Scope sounds_;
    };
    Scope activate() { return Scope(*this); }
    // Reject incomplete bindings before any original game code can run.
    ClientGameModule::Shell create_shell(const ClientGameModule&);
    ClientScene& scene();
    void begin_frame(float elapsed);
    LTRESULT finish_frame();
    InputFrame input_frame(std::span<const platform::InputEvent>,std::optional<platform::GamepadState> = {});
    LTRESULT SetCommandToggle(int,LTBOOL) override;
    HMESSAGEWRITE StartHMessageWrite() override;
    HMODELANIM GetAnimIndex(HOBJECT,char*) override;
    void SetModelAnimation(HOBJECT,HMODELANIM) override;
    HMODELANIM GetModelAnimation(HOBJECT) override;
    void SetModelLooping(HOBJECT,LTBOOL) override;
    LTBOOL GetModelLooping(HOBJECT) override;
    void SetModelPlaying(HOBJECT,LTBOOL) override;
    LTBOOL GetModelPlaying(HOBJECT) override;
    LTRESULT ResetModelAnimation(HOBJECT) override;
    uint32 GetModelPlaybackState(HOBJECT) override;
    LTRESULT FreeUnusedModels() override;
    void CPrint(char*,...) override;
    uint32 GetPointContainers(LTVector*,HOBJECT*,uint32) override;
    LTBOOL GetContainerCode(HOBJECT,uint16*) override;
    LTRESULT OpenFile(char*,ILTStream**) override;
    LTRESULT CopyFile(const char*,const char*) override;
    HSTRING FormatString(int,...) override;
    HSTRING CopyString(HSTRING) override;
    HSTRING CreateString(char*) override;
    void FreeString(HSTRING) override;
    LTBOOL CompareStrings(HSTRING,HSTRING) override;
    LTBOOL CompareStringsUpper(HSTRING,HSTRING) override;
    char* GetStringData(HSTRING) override;
    float GetVarValueFloat(HCONSOLEVAR) override;
    char* GetVarValueString(HCONSOLEVAR) override;
    LTRESULT SetConsoleOutputFn(ConsoleOutputFn,void*) override;
    LTRESULT EnumConsoleCommands(ConsoleCommandEnumFn,void*) override;
    LTFLOAT GetTime() override;
    LTFLOAT GetFrameTime() override;
    LTRESULT RemoveObject(HOBJECT) override;
    LTRESULT GetPointStatus(LTVector*) override;
    LTRESULT GetPointShade(LTVector*,LTVector*) override;
    LTRESULT GetAttachments(HLOCALOBJ,HLOCALOBJ*,uint32,uint32*,uint32*) override;
    LTRESULT ProcessAttachments(HOBJECT) override;
    LTRESULT SetObjectPos(HLOCALOBJ,LTVector*,LTBOOL=LTFALSE) override;
    uint32 GetObjectFlags(HOBJECT) override;
    void SetObjectFlags(HOBJECT,uint32) override;
    LTRESULT ModelNodeControl(HOBJECT,NodeControlFn,void*) override;
    LTRESULT GetSpriteControl(HLOCALOBJ,ILTSpriteControl*&) override;
    LTParticle* AddParticle(HLOCALOBJ,LTVector*,LTVector*,LTVector*,float) override;
    LTRESULT GetCanvasFn(HOBJECT,CanvasDrawFn&,void*&) override;
    LTRESULT SetCanvasFn(HOBJECT,CanvasDrawFn,void*) override;
    LTRESULT GetCanvasRadius(HOBJECT,float&) override;
    LTRESULT SetCanvasRadius(HOBJECT,float) override;
protected:
    ClientEngineBase(std::shared_ptr<ClientScene>,std::shared_ptr<const Assets>,
        std::shared_ptr<const Resources>,std::shared_ptr<ClientTextures>,platform::UserFiles = {});
    void require_owner() const;
    void validate_sdk() const;
    // The engine whose callback scope is active on this thread, if any. SDK
    // function pointers carry no context and find their engine through it.
    static std::shared_ptr<ClientEngineBase> active_engine();
    ConsoleState& console() { return console_; }
    virtual ClientSurfaces& surface_service()=0;
    virtual ClientFonts& font_service()=0;
    virtual ClientInput& input_service()=0;
    virtual ClientSounds& sound_service()=0;
    std::shared_ptr<const void> game_code_;
    std::shared_ptr<ClientScene> scene_;
    std::shared_ptr<const Assets> assets_;
    std::shared_ptr<const Resources> resources_;
    std::shared_ptr<ClientTextures> textures_;
    platform::UserFiles writable_;
private:
    template<class F> static LTRESULT sdk(F&&);
    template<class T,class F> static T sdk_value(F&&,T fallback={});
    static thread_local std::shared_ptr<ClientEngineBase> current_;
    static void console_print(char*,...);
    static void input_command(const char*,int,char**);
    static void save_input(std::string&);
    LTAnimTracker* main_tracker(HOBJECT);
    ConsoleState console_;
    ConsoleSaverFn input_saver_=save_input;
    ConsoleOutputFn output_=nullptr;
    void* output_user_=nullptr;
    bool printing_=false;
    const std::thread::id owner_=std::this_thread::get_id();
    const std::uint64_t started_;
    float elapsed_=0;
    bool finishing_=false;
    std::vector<MoveObjectReference> removed_;
    std::vector<MoveObjectReference> draining_;
    std::size_t drain_cursor_=0;
};
}
