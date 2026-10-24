#include "engine/client/client_engine_base.h"
#include "engine/client/client_message_io.h"
#include "engine/client/client_effects.h"
#include "engine/server/message.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/message_format.h"
#include "engine/server/commonmodel.h"
#include "engine/server/stringbank.h"
#include "engine/server/objtypes.h"
#include "engine/server/counter.h"
#include "engine/server/filemgr.h"
#include "engine/server/world_loader.h"
#include "engine/portable/string.h"
#include "platform/logging.h"
#include "platform/system.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace nolf {
namespace {
template<class F> LTRESULT result(F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
}
static_assert(std::is_abstract_v<ClientEngineBase>);
thread_local std::shared_ptr<ClientEngineBase> ClientEngineBase::current_;
template<class F> LTRESULT ClientEngineBase::sdk(F&& function) {
    const auto self=current_;
    if (!self) return LT_NOTINITIALIZED;
    return result([&] { self->require_owner(); return function(*self); });
}
template<class T,class F> T ClientEngineBase::sdk_value(F&& function,T fallback) {
    T output=fallback; sdk([&](ClientEngineBase& self) -> LTRESULT { output=function(self); return LT_OK; }); return output;
}
void ClientEngineBase::console_print(char* format,...) {
    if (!format) return;
    std::array<char,4096> text; va_list arguments; va_start(arguments,format);
    const int length=std::vsnprintf(text.data(),text.size(),format,arguments); va_end(arguments);
    if (length>=0) sdk([&](ClientEngineBase& self) -> LTRESULT { char message[]="%s"; self.CPrint(message,text.data()); return LT_OK; });
}
ClientEngineBase::ClientEngineBase(std::shared_ptr<ClientScene> scene,std::shared_ptr<const Assets> assets,
    std::shared_ptr<const Resources> resources,std::shared_ptr<ClientTextures> textures,platform::UserFiles writable)
    : ILTClient{},scene_(std::move(scene)),assets_(std::move(assets)),resources_(std::move(resources)),
      textures_(std::move(textures)),writable_(std::move(writable)),started_(platform::monotonic_nanoseconds()) {
    if (!scene_ || !assets_ || !resources_ || !textures_) throw std::invalid_argument("native client services are required");
    m_pCommonLT=&scene_->common(); m_pPhysicsLT=&scene_->physics();
    m_pModelLT=&scene_->model(); m_pTransformLT=&scene_->transform();
    m_pLightAnimLT=&scene_->lighting();
    m_pTexMod=textures_.get();
    scene_->install(*this);
    ClientFonts::install(*this);
    ClientInput::install(*this);
    ClientSounds::install(*this);
    if (!con_Init(&console_)) throw std::bad_alloc();
    console_.m_pPrint=console_print;
    console_.m_pSavers=&input_saver_; console_.m_nSavers=1;
#define NOLF_INPUT_COMMAND(name) if (!con_AddCommand(&console_,#name,+[](int count,char** args) { input_command(#name,count,args); },nullptr)) throw std::bad_alloc();
    NOLF_INPUT_COMMAND(AddAction)
    NOLF_INPUT_COMMAND(EnableDevice)
    NOLF_INPUT_COMMAND(rangebind)
    NOLF_INPUT_COMMAND(scale)
    NOLF_INPUT_COMMAND(rangescale)
#undef NOLF_INPUT_COMMAND
    RegisterConsoleProgram=+[](char* name,ConsoleProgramFn function) -> LTRESULT {
        return sdk([&](ClientEngineBase& self) -> LTRESULT {
            if (!name || !*name || !function) return LT_INVALIDPARAMS;
            for (auto* link=self.console_.m_CommandHead.m_pNext;link!=&self.console_.m_CommandHead;link=link->m_pNext)
                if (ascii_compare(static_cast<ConsoleCommand*>(link->m_pData)->m_Name,name)==0) return LT_ALREADYEXISTS;
            return con_AddCommand(&self.console_,name,function,nullptr) ? LT_OK : LT_OUTOFMEMORY;
        });
    };
    UnregisterConsoleProgram=+[](char* name) -> LTRESULT {
        return sdk([&](ClientEngineBase& self) -> LTRESULT {
            if (!name) return LT_INVALIDPARAMS;
            for (auto* link=self.console_.m_CommandHead.m_pNext;link!=&self.console_.m_CommandHead;link=link->m_pNext) {
                auto* command=static_cast<ConsoleCommand*>(link->m_pData);
                if (ascii_compare(command->m_Name,name)==0) { con_RemoveCommand(&self.console_,command); return LT_OK; }
            }
            return LT_NOTFOUND;
        });
    };
    GetConsoleVar=+[](char* name) -> HCONSOLEVAR { return sdk_value<HCONSOLEVAR>([&](ClientEngineBase& self) { return name ? con_FindVar(&self.console_,name) : nullptr; }); };
    RunConsoleString=+[](char* text) { sdk([&](ClientEngineBase& self) -> LTRESULT { con_RunString(&self.console_,text); return self.console_.m_Result; }); };
    ReadConfigFile=+[](char* name) -> LTRESULT {
        return sdk([&](ClientEngineBase& self) -> LTRESULT {
            if (!name) return LT_INVALIDPARAMS;
            std::vector<std::byte> bytes;
            if (self.writable_.file_exists(name)) bytes=self.writable_.read(name,16*1024*1024);
            else {
                const auto source=self.assets_->locate(name);
                if (source.size>16*1024*1024) return LT_INVALIDPARAMS;
                bytes.resize(std::size_t(source.size)); source.read_at(0,bytes);
            }
            if (bytes.size()>16*1024*1024 || std::find(bytes.begin(),bytes.end(),std::byte{0})!=bytes.end()) return LT_INVALIDPARAMS;
            std::string text;
            if (!bytes.empty()) text.assign(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            con_RunString(&self.console_,text.c_str()); return self.console_.m_Result;
        });
    };
    WriteConfigFile=+[](char* name) -> LTRESULT {
        return sdk([&](ClientEngineBase& self) -> LTRESULT {
            if (!name) return LT_INVALIDPARAMS;
            const auto native=self.writable_.path(name).u8string();
            return con_SaveFile(&self.console_,reinterpret_cast<const char*>(native.c_str())) ? LT_OK : LT_ERROR;
        });
    };
    StartCounter=ci_StartCounter; EndCounter=ci_EndCounter;
    GetFileList=+[](char* directory) -> FileEntry* { return sdk_value<FileEntry*>([&](ClientEngineBase& self) { return fs_GetAssetFileList(*self.assets_,directory); }); };
    FreeFileList=fs_FreeFileList;
    GetWorldInfoString=+[](char* filename,char* output,uint32 capacity,uint32* actual) -> LTRESULT {
        if (actual) *actual=0;
        if (capacity && !output) return LT_INVALIDPARAMS;
        if (capacity) output[0]='\0';
        return sdk([&](ClientEngineBase& self) -> LTRESULT {
            if (!filename) return LT_INVALIDPARAMS;
            auto name=platform::asset_key(filename); if (!name.ends_with(".dat")) name+=".dat";
            if (!self.assets_->contains(name)) return LT_NOTFOUND;
            try {
                AssetStream stream(self.assets_->locate(name)); const auto info=w_ReadWorldInformation(stream);
                if (actual) *actual=static_cast<uint32>(info.information.size());
                if (capacity) {
                    const auto count=std::min<std::size_t>(info.information.size(),capacity-1);
                    std::copy_n(info.information.data(),count,output); output[count]='\0';
                }
                return LT_OK;
            } catch (const std::bad_alloc&) { throw; }
            catch (...) { return LT_INVALIDWORLDFILE; }
        });
    };
}
ClientEngineBase::~ClientEngineBase() = default;
LTParticle* ClientEngineBase::AddParticle(HLOCALOBJ object,LTVector* position,LTVector* velocity,LTVector* color,float lifetime) {
    LTParticle* output=nullptr;
    result([&]() -> LTRESULT { require_owner(); if (!position || !velocity || !color) return LT_INVALIDPARAMS;
        output=scene_->effects().add_particle(object,*position,*velocity,*color,lifetime); return LT_OK; });
    return output;
}
LTRESULT ClientEngineBase::GetCanvasFn(HOBJECT object,CanvasDrawFn& function,void*& user) {
    function=nullptr; user=nullptr; return result([&]() -> LTRESULT { require_owner(); const auto canvas=scene_->canvas(object); if (!canvas) return LT_INVALIDPARAMS; function=canvas->draw; user=canvas->user; return LT_OK; });
}
LTRESULT ClientEngineBase::SetCanvasFn(HOBJECT object,CanvasDrawFn function,void* user) { require_owner(); return scene_->canvas_function(object,function,user); }
LTRESULT ClientEngineBase::GetCanvasRadius(HOBJECT object,float& radius) {
    radius=0; return result([&]() -> LTRESULT { require_owner(); const auto canvas=scene_->canvas(object); if (!canvas) return LT_INVALIDPARAMS; radius=canvas->radius; return LT_OK; });
}
LTRESULT ClientEngineBase::SetCanvasRadius(HOBJECT object,float radius) { require_owner(); return scene_->canvas_radius(object,radius); }
std::shared_ptr<ClientEngineBase> ClientEngineBase::active_engine() { return current_; }
void ClientEngineBase::require_owner() const {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("native client engine used from another thread");
}
ClientEngineBase::Scope::Scope(ClientEngineBase& engine) : engine_(engine.shared_from_this()),previous_(current_),scene_(*engine.scene_),surfaces_(engine.surface_service()),fonts_(engine.font_service()),input_(engine.input_service()),sounds_(engine.sound_service()) { engine.require_owner(); current_=engine_; }
ClientEngineBase::Scope::~Scope() { current_=std::move(previous_); }
void ClientEngineBase::validate_sdk() const {
    require_owner();
#define NOLF_CLIENT_API(name) if (!name) throw std::logic_error("native client SDK binding is missing: " #name);
#include "client_engine_members.inc"
#undef NOLF_CLIENT_API
    if (!m_pCommonLT || !m_pPhysicsLT || !m_pModelLT || !m_pTransformLT || !m_pLightAnimLT || !m_pSoundMgr ||
        !m_pVideoMgr || !m_pTexMod || !m_pCursorLT || !m_pDirectMusicMgr)
        throw std::logic_error("native client subsystem interfaces are incomplete");
}
ClientGameModule::Shell ClientEngineBase::create_shell(const ClientGameModule& module) {
    const auto scope=activate(); validate_sdk();
    if (game_code_) throw std::logic_error("native client host already created its game shell");
    game_code_=module.lease(); return module.create_shell(*this);
}
ClientScene& ClientEngineBase::scene() { require_owner(); return *scene_; }
void ClientEngineBase::input_command(const char* name,int count,char** args) {
    sdk([&](ClientEngineBase& self) -> LTRESULT {
        if (count<0 || count>30) return LT_INVALIDPARAMS;
        std::vector<std::string_view> arguments; arguments.reserve(std::size_t(count));
        for (int i=0;i<count;++i) arguments.emplace_back(args[i]);
        self.console_.m_Result=self.input_service().command(name,arguments); return self.console_.m_Result;
    });
}
void ClientEngineBase::save_input(std::string& text) { sdk([&](ClientEngineBase& self) -> LTRESULT { text+=self.input_service().config(); return LT_OK; }); }
LTRESULT ClientEngineBase::SetCommandToggle(int code,LTBOOL enabled) { require_owner(); return input_service().toggle(code,enabled!=0); }
InputFrame ClientEngineBase::input_frame(std::span<const platform::InputEvent> events,std::optional<platform::GamepadState> gamepad) {
    const auto scope=activate(); auto frame=input_service().frame(events,gamepad);
    for (const auto& [name,value] : frame.variables) { std::array<char,64> text{}; std::snprintf(text.data(),text.size(),"%.9g",double(value)); con_SetVar(&console_,name.c_str(),text.data()); }
    for (const auto& command : frame.console) con_RunString(&console_,command.c_str());
    return frame;
}
void ClientEngineBase::begin_frame(float elapsed) {
    require_owner();
    if (!std::isfinite(elapsed) || elapsed<0 || elapsed>1) throw std::invalid_argument("invalid native client frame time");
    if (finishing_) throw std::logic_error("native client frame started during retirement");
    elapsed_=elapsed;
}
LTRESULT ClientEngineBase::finish_frame() {
    return result([&]() -> LTRESULT {
        require_owner(); const auto active=activate();
        if (finishing_) return LT_NOCHANGE;
        finishing_=true; struct Reset { bool& flag; ~Reset() { flag=false; } } reset{finishing_};
        LTRESULT status=LT_OK;
        // Work queued by removal callbacks belongs to the next boundary.
        // The current snapshot cannot grow indefinitely through game code.
        if (draining_.empty()) { draining_.swap(removed_); drain_cursor_=0; }
        for (;drain_cursor_<draining_.size();++drain_cursor_) {
            const auto& entry=draining_[drain_cursor_];
            if (!entry.guard.same_data() || !scene_->contains(entry.object)) continue;
            const auto removed=scene_->remove(entry.object);
            if (removed!=LT_OK && removed!=LT_NOCHANGE) {
                if (status==LT_OK) status=removed;
                // A pre-callback allocation failure leaves the object live.
                // Keep this exact identity and cursor for a later retry.
                if (scene_->contains(entry.object)) return status;
            }
        }
        draining_.clear(); drain_cursor_=0;
        return status;
    });
}
LTRESULT ClientEngineBase::RemoveObject(HOBJECT object) {
    return result([&]() -> LTRESULT {
        require_owner(); if (!scene_->contains(object)) return LT_INVALIDPARAMS;
        if (object->m_ChangeFlags&1) return LT_NOCHANGE;
        removed_.emplace_back(object); object->m_ChangeFlags|=1; return LT_OK;
    });
}
HMESSAGEWRITE ClientEngineBase::StartHMessageWrite() {
    require_owner(); const auto keep=shared_from_this(); ILTMessage* message=nullptr;
    const auto context=client_message_io(scene_,resources_,keep);
    if (scene_->common().CreateMessage(message)!=LT_OK) return nullptr;
    static_cast<LMessageImpl*>(message)->SetObjectIO(context); return message;
}
LTAnimTracker* ClientEngineBase::main_tracker(HOBJECT object) {
    require_owner(); LTAnimTracker* tracker=nullptr;
    return scene_->model().GetMainTracker(object,tracker)==LT_OK ? tracker : nullptr;
}
HMODELANIM ClientEngineBase::GetAnimIndex(HOBJECT object,char* name) {
    require_owner(); if (!scene_->contains(object) || !name) return INVALID_MODEL_ANIM;
    return ci_GetAnimIndex(object,name);
}
void ClientEngineBase::SetModelAnimation(HOBJECT object,HMODELANIM animation) { if (auto* tracker=main_tracker(object)) scene_->model().SetCurAnim(tracker,animation); }
HMODELANIM ClientEngineBase::GetModelAnimation(HOBJECT object) { HMODELANIM animation=INVALID_MODEL_ANIM; if (auto* tracker=main_tracker(object)) scene_->model().GetCurAnim(tracker,animation); return animation; }
void ClientEngineBase::SetModelLooping(HOBJECT object,LTBOOL loop) { if (auto* tracker=main_tracker(object)) scene_->model().SetLooping(tracker,loop); }
LTBOOL ClientEngineBase::GetModelLooping(HOBJECT object) { auto* tracker=main_tracker(object); return tracker && scene_->model().GetLooping(tracker)==LT_YES; }
void ClientEngineBase::SetModelPlaying(HOBJECT object,LTBOOL playing) { if (auto* tracker=main_tracker(object)) scene_->model().SetPlaying(tracker,playing); }
LTBOOL ClientEngineBase::GetModelPlaying(HOBJECT object) { auto* tracker=main_tracker(object); return tracker && scene_->model().GetPlaying(tracker)==LT_YES; }
LTRESULT ClientEngineBase::ResetModelAnimation(HOBJECT object) { auto* tracker=main_tracker(object); return tracker ? scene_->model().ResetAnim(tracker) : LT_INVALIDPARAMS; }
uint32 ClientEngineBase::GetModelPlaybackState(HOBJECT object) { uint32 state=0; if (auto* tracker=main_tracker(object)) scene_->model().GetPlaybackState(tracker,state); return state; }
LTRESULT ClientEngineBase::FreeUnusedModels() { require_owner(); scene_->free_unused_models(); return LT_OK; }
void ClientEngineBase::CPrint(char* format,...) {
    require_owner(); if (!format) return;
    const auto active=activate();
    std::array<char,65537> text;
    va_list arguments; va_start(arguments,format);
    const int length=std::vsnprintf(text.data(),text.size(),format,arguments); va_end(arguments);
    if (length>=0) {
        platform::log("client",std::string_view(text.data(),std::min<std::size_t>(std::size_t(length),text.size()-1)));
        if (output_ && !printing_) {
            printing_=true; struct Reset { bool& value; ~Reset() { value=false; } } reset{printing_};
            const auto callback=output_; auto* user=output_user_; callback(user,0xffffff,0,text.data());
        }
    }
}
uint32 ClientEngineBase::GetPointContainers(LTVector* point,HOBJECT* objects,uint32 capacity) { require_owner(); return point ? scene_->point_containers(*point,objects,capacity) : 0; }
LTBOOL ClientEngineBase::GetContainerCode(HOBJECT object,uint16* code) {
    require_owner(); if (!code) return LTFALSE; *code=0;
    if (!scene_->contains(object) || object->m_ObjectType!=OT_CONTAINER) return LTFALSE;
    *code=static_cast<CObjContainer*>(object)->m_ContainerCode; return LTTRUE;
}
LTRESULT ClientEngineBase::OpenFile(char* filename,ILTStream** output) {
    if (!output) return LT_INVALIDPARAMS;
    *output=nullptr;
    return result([&]() -> LTRESULT { require_owner(); if (!filename) return LT_INVALIDPARAMS;
        *output=new AssetStream(assets_->locate(filename)); return LT_OK; });
}
LTRESULT ClientEngineBase::CopyFile(const char* source,const char* destination) {
    return result([&]() -> LTRESULT { require_owner(); if (!source || !destination) return LT_INVALIDPARAMS;
        const auto input=assets_->locate(source); platform::AtomicOutputFile output(writable_.path(destination));
        std::array<std::byte,65536> bytes;
        for (std::uint64_t at=0;at<input.size;) {
            const auto size=std::size_t(std::min<std::uint64_t>(bytes.size(),input.size-at));
            input.read_at(at,std::span(bytes).first(size)); output.write(std::span(bytes).first(size)); at+=size;
        }
        output.commit(); return LT_OK; });
}
HSTRING ClientEngineBase::FormatString(int id,...) {
    require_owner(); va_list arguments; va_start(arguments,id);
    HSTRING output=nullptr;
    try { auto formatted=format_resource({resources_.get(),"cres",1033},id,&arguments); output=sb_CreateString(formatted.data()); }
    catch (...) { va_end(arguments); return nullptr; }
    va_end(arguments); return output;
}
HSTRING ClientEngineBase::CopyString(HSTRING value) { require_owner(); return sb_CopyString(value); }
HSTRING ClientEngineBase::CreateString(char* value) { require_owner(); return sb_CreateString(value); }
void ClientEngineBase::FreeString(HSTRING value) { require_owner(); sb_FreeString(value); }
LTBOOL ClientEngineBase::CompareStrings(HSTRING a,HSTRING b) { require_owner(); return sb_CompareStrings(a,b); }
LTBOOL ClientEngineBase::CompareStringsUpper(HSTRING a,HSTRING b) { require_owner(); return sb_CompareStringsUpper(a,b); }
char* ClientEngineBase::GetStringData(HSTRING value) {
    // A null string reads as empty: game code copies a message's optional
    // strings (a lip-sync stop sends none) without checking.
    require_owner(); static char empty[1]{}; auto* data=sb_GetStringData(value); return data ? data : empty;
}
float ClientEngineBase::GetVarValueFloat(HCONSOLEVAR handle) {
    require_owner(); auto* variable=reinterpret_cast<LTCommandVar*>(handle);
    return console_.m_Variables.Contains(variable) ? variable->m_FloatVal : 0;
}
char* ClientEngineBase::GetVarValueString(HCONSOLEVAR handle) {
    require_owner(); auto* variable=reinterpret_cast<LTCommandVar*>(handle);
    return console_.m_Variables.Contains(variable) ? variable->m_pStringVal : nullptr;
}
LTRESULT ClientEngineBase::SetConsoleOutputFn(ConsoleOutputFn callback,void* user) { require_owner(); output_=callback; output_user_=user; return LT_OK; }
LTRESULT ClientEngineBase::EnumConsoleCommands(ConsoleCommandEnumFn callback,void* user) {
    return result([&]() -> LTRESULT {
        require_owner(); const auto active=activate(); if (!callback) return LT_INVALIDPARAMS;
        std::vector<std::string> names;
        for (auto* link=console_.m_CommandHead.m_pPrev;link!=&console_.m_CommandHead;link=link->m_pPrev) names.push_back(static_cast<ConsoleCommand*>(link->m_pData)->m_Name);
        for (const auto& name : names) callback(user,name.c_str(),1);
        return LT_OK;
    });
}
LTFLOAT ClientEngineBase::GetTime() { require_owner(); return float(double(platform::monotonic_nanoseconds()-started_)/1e9); }
LTFLOAT ClientEngineBase::GetFrameTime() { require_owner(); return elapsed_; }
LTRESULT ClientEngineBase::GetPointStatus(LTVector* point) { require_owner(); return scene_->common().GetPointStatus(point); }
LTRESULT ClientEngineBase::GetPointShade(LTVector* point,LTVector* color) { require_owner(); return scene_->common().GetPointShade(point,color); }
LTRESULT ClientEngineBase::GetAttachments(HLOCALOBJ object,HLOCALOBJ* objects,uint32 capacity,uint32* count,uint32* total) {
    if (!count || !total) return LT_INVALIDPARAMS;
    require_owner(); return scene_->common().GetAttachments(object,objects,capacity,*count,*total);
}
LTRESULT ClientEngineBase::ProcessAttachments(HOBJECT object) { const auto active=activate(); require_owner(); return scene_->process_attachments(object); }
LTRESULT ClientEngineBase::SetObjectPos(HLOCALOBJ object,LTVector* position,LTBOOL force) { const auto active=activate(); require_owner(); return position ? scene_->position(object,*position,force!=LTFALSE) : LT_INVALIDPARAMS; }
uint32 ClientEngineBase::GetObjectFlags(HOBJECT object) { require_owner(); uint32 flags=0; scene_->common().GetObjectFlags(object,OFT_Flags,flags); return flags; }
void ClientEngineBase::SetObjectFlags(HOBJECT object,uint32 flags) { require_owner(); scene_->common().SetObjectFlags(object,OFT_Flags,flags); }
LTRESULT ClientEngineBase::ModelNodeControl(HOBJECT object,NodeControlFn function,void* user) { require_owner(); return scene_->node_control(object,function,user); }
LTRESULT ClientEngineBase::GetSpriteControl(HLOCALOBJ object,ILTSpriteControl*& output) { require_owner(); return scene_->sprite_control(object,output); }
}
