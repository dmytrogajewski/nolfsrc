#include "PolyGridFX.h"
#include "ClientServerShared.h"
#include "engine/server/objtypes.h"
#include "engine/client/native_client.h"
#include "platform/audio.h"
#include "platform/fonts.h"
#include "platform/music.h"
#include "platform/video.h"
#include "platform/system.h"
#include <fstream>
#include <cstring>
#include <iostream>

ILTClient* g_pLTClient=nullptr;
class CGameClientShell;
class CInterfaceMgr;
CGameClientShell* g_pGameClientShell=nullptr;
CInterfaceMgr* g_pInterfaceMgr=nullptr;
// Global detail settings are disabled in this isolated FX fixture.
int GetConsoleInt(char*,int fallback) { return fallback; }
namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
class WaterFX final : public CPolyGridFX {
public:
    void bind(ILTClient* client,HOBJECT server,HOBJECT surface) {
        m_pClientDE=client; m_hServerObject=server; m_hObject=surface;
        SetUseGlobalSettings(LTFALSE);
    }
};
void checks() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-game-polygrid-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::filesystem::create_directories(root/"resources/cres/1033/strings");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path,e); } } cleanup{root};
    std::ofstream(root/"resources/cres/1033/strings/1.txt")<<"fixture";
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(root);
    nolf::platform::DesktopConfig config; config.width=128; config.height=128; config.vsync=false;
    nolf::platform::Desktop desktop(config);
    nolf::NativeClientServices services{assets,std::make_shared<nolf::Resources>(root),
        nolf::platform::make_freetype_fonts(nolf::platform::system_font_locator()),
        nolf::platform::make_ffmpeg_audio,nolf::platform::make_sdl_audio,nolf::platform::make_ffmpeg_video,
        nolf::platform::make_dmusic_performance,{},root,{}};
    auto client=nolf::NativeClient::create(desktop,std::move(services)); auto scope=client->activate();
    g_pLTClient=client.get();

    ObjectCreateStruct create; create.m_ObjectType=OT_POLYGRID;
    create.m_Pos.Init(0,-100,0); create.m_Flags=FLAG_VISIBLE;
    const auto server=client->CreateObject(&create);
    const auto surface=client->CreateObject(&create);
    require(server && surface,"water objects were not created");
    client->SetObjectUserFlags(server,USRFLG_VISIBLE);
    {
        WaterFX water; water.bind(client.get(),server,surface);
        require(!(client->GetObjectFlags(surface)&FLAG_WASDRAWN),"fixture water was already drawn");
        LTVector rising(0,200,0),actual;
        client->SetObjectPos(server,&rising);
        require(water.Update(),"undrawn rising water was removed");
        client->GetObjectPos(surface,&actual);
        require(actual.DistSqr(rising)<.0001f,"undrawn visual water stayed below the rising volume");
        client->SetObjectUserFlags(server,0);
        rising.y=300; client->SetObjectPos(server,&rising);
        require(water.Update(),"hidden rising water was removed");
        client->GetObjectPos(surface,&actual);
        require(actual.DistSqr(rising)<.0001f && !(client->GetObjectFlags(surface)&FLAG_VISIBLE),
            "hidden water did not follow its volume");
        client->SetObjectUserFlags(server,USRFLG_VISIBLE);
        require(water.Update() && (client->GetObjectFlags(surface)&FLAG_VISIBLE),"water did not reappear at its new height");
    }
    client->DeleteObject(server); g_pLTClient=nullptr;
}
}
int main() { try { checks(); std::cout<<"Game water follows rising volumes while undrawn or hidden\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
