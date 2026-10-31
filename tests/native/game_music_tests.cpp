#include "Music.h"
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
namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void checks() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-game-music-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::filesystem::create_directories(root/"Music");
    std::filesystem::create_directories(root/"resources/cres/1033/strings");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path,e); } } cleanup{root};
    std::ofstream(root/"resources/cres/1033/strings/1.txt")<<"fixture";
    // Empty intensities isolate delivery from the synthesizer's segment data.
    std::ofstream(root/"Music/Test.txt")<<"NUMINTENSITIES 5\nINITIALINTENSITY 1\nINITIALVOLUME 0\n";
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_directory(root);
    nolf::platform::DesktopConfig config; config.width=128; config.height=128; config.vsync=false;
    nolf::platform::Desktop desktop(config);
    nolf::NativeClientServices services{assets,std::make_shared<nolf::Resources>(root),
        nolf::platform::make_freetype_fonts(nolf::platform::system_font_locator()),
        nolf::platform::make_ffmpeg_audio,nolf::platform::make_sdl_audio,nolf::platform::make_ffmpeg_video,
        nolf::platform::make_dmusic_performance,{},root,{}};
    auto client=nolf::NativeClient::create(desktop,std::move(services)); auto scope=client->activate();
    g_pLTClient=client.get();
    CMusic music; require(music.Init(client.get()),"game music initialization failed");
    char request[]="music i 5 measure";
    music.ProcessMusicMessage(request);
    request[8]='2'; // The queued command must own its text.
    require(!music.IsPlaying(),"preload command started without a level");
    CMusicState level; std::strcpy(level.szDirectory,"Music"); std::strcpy(level.szControlFile,"Test.txt");
    require(music.RestoreMusicState(level) && music.IsPlaying(),"preload command was dropped");
    require(client->GetDirectMusicMgr()->GetCurIntensity()==5,"preload command lost its intensity or owned text");
    music.TermLevel();
    char next[]="music i 3 measure";
    const auto message=client->StartHMessageWrite(); const auto text=client->CreateString(next);
    client->WriteToMessageHString(message,text); client->FreeString(text);
    const auto read=reinterpret_cast<HMESSAGEREAD>(message); client->ResetRead(read);
    music.ProcessMusicMessage(read); client->EndHMessageWrite(message);
    require(music.RestoreMusicState(level) && music.IsPlaying() && client->GetDirectMusicMgr()->GetCurIntensity()==3,
        "world transition dropped its startup music");
    music.TermLevel(); music.ProcessMusicMessage(next); music.TermLevel();
    require(music.RestoreMusicState(level) && !music.IsPlaying(),"cancelled load replayed stale music commands");
    music.Term(); require(music.Init(client.get()),"music reinitialization failed");
    music.ProcessMusicMessage(next);
    require(music.RestoreMusicState(level) && music.IsPlaying() && client->GetDirectMusicMgr()->GetCurIntensity()==3,
        "music reinitialization retained an unloaded control file");
    music.Term(); g_pLTClient=nullptr;
}
}
int main() { try { checks(); std::cout<<"Game music retains preload triggers and resets between worlds\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
