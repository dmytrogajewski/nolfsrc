#include "engine/client/client_world_sync.h"
#include "engine/portable/assets.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* error) { if (!value) throw std::runtime_error(error); }
struct Bytes {
    std::vector<std::byte> data;
    Bytes& u8(std::uint8_t v) { data.push_back(std::byte(v)); return *this; }
    Bytes& u16(std::uint16_t v) { return u8(v&0xff).u8(v>>8); }
    Bytes& u32(std::uint32_t v) { return u16(v&0xffff).u16(v>>16); }
    Bytes& f32(float v) { return u32(std::bit_cast<std::uint32_t>(v)); }
    Bytes& vector(float x,float y,float z) { return f32(x).f32(y).f32(z); }
    Bytes& text(const char* s) { while (*s) u8(std::uint8_t(*s++)); return u8(0); }
};
// A tracked sound record: no object flags, type 1, its change bits and ID.
Bytes tracked(std::uint16_t id,std::uint8_t change) { Bytes b; b.u8(0).u8(1).u8(change).u16(id); return b; }
void checks() {
    const auto root=std::filesystem::temp_directory_path()/("nolf-world-sync-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    const std::string text="x";
    nolf::platform::atomic_write(root/"resources/cres/1033/strings/1.txt",std::as_bytes(std::span(text)));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path,e); } } cleanup{root};
    nolf::ServerFiles files(std::make_shared<nolf::Assets>());
    { Bytes announce; announce.u16(5).u32(10).text("snd/phone.wav"); files.announce(announce.data); }
    std::vector<std::string> played; std::vector<HLTSOUND> stopped; std::uintptr_t next=1;
    nolf::ClientSyncEvents events;
    events.sound=[&](PlaySoundInfo& info,const std::string& file,float) -> HLTSOUND {
        require(info.m_dwFlags&PLAYSOUND_GETHANDLE,"tracked server sound played without a handle");
        played.push_back(file); return reinterpret_cast<HLTSOUND>(next++);
    };
    events.stop_sound=[&](HLTSOUND sound) { stopped.push_back(sound); };
    auto engine=std::make_shared<int>(0);
    nolf::ClientWorldSync sync(std::make_shared<nolf::ClientScene>(),std::make_shared<nolf::Resources>(root),engine,events);
    // New tracked sound 40 (file 5, looping, no optional fields). Moves need a
    // world's position reference, which this fixture has none of.
    auto start=tracked(40,0x1); start.u16(5).u8(1).u16(PLAYSOUND_LOOP).u32(0);
    sync.apply(8,start.data,files);
    require(played.size()==1 && played[0]=="snd/phone.wav","tracked server sound did not play");
    // The server removes ID 40: the loop stops.
    Bytes removal; removal.u8(0).u8(3).u16(40);
    sync.apply(8,removal.data,files);
    require(stopped.size()==1 && stopped[0]==reinterpret_cast<HLTSOUND>(1),"removed server sound kept playing");
    // A sound still playing when the world resets stops with it.
    auto again=tracked(41,0x1); again.u16(5).u8(1).u16(PLAYSOUND_LOOP).u32(0);
    sync.apply(8,again.data,files); sync.reset();
    require(stopped.size()==2 && stopped[1]==reinterpret_cast<HLTSOUND>(2),"world reset left a server sound playing");
    // A one-shot sound sends its flags only when they change, and the server
    // remembers what it sent for the whole connection: after a world reset
    // the next play of the same file still parses with them.
    std::vector<std::pair<uint8,uint32>> heard;
    events={}; events.sound=[&](PlaySoundInfo& info,const std::string&,float) -> HLTSOUND { heard.push_back({info.m_nVolume,info.m_UserData}); return nullptr; };
    nolf::ClientWorldSync one_shots(std::make_shared<nolf::ClientScene>(),std::make_shared<nolf::Resources>(root),engine,events);
    Bytes first; first.u8(0).u8(0).u16(5).u8(1).u16(PLAYSOUND_CTRL_VOL).u8(40).u32(7);
    one_shots.apply(8,first.data,files); one_shots.reset();
    Bytes repeat; repeat.u8(0).u8(0).u16(5).u8(0).u8(60).u32(9);
    one_shots.apply(8,repeat.data,files);
    require(heard.size()==2 && heard[0]==std::pair<uint8,uint32>{40,7} && heard[1]==std::pair<uint8,uint32>{60,9},"world reset forgot a sound's sent flags");

    auto scene=std::make_shared<nolf::ClientScene>();
    nolf::ClientWorldSync objects(scene,std::make_shared<nolf::Resources>(root),engine,{});
    const auto create=[&](uint16 id) {
        Bytes record; record.u8(0x09).u16(id).u8(OT_NORMAL)
            .u16(FLAG_VISIBLE|FLAG_FULLPOSITIONRES).u8(0).u32(0).vector(0,0,0).vector(0,0,0);
        objects.apply(8,record.data,files);
    };
    const auto attach=[&](uint16 parent,uint16 child) {
        Bytes record; record.u8(0x80).u8(1).u16(parent).u16(child).u32(0xffffffff).vector(2,3,4)
            .u8(0).u8(0).u8(127).u8(0).u8(127).u8(0).u16(0xffff);
        objects.apply(8,record.data,files);
    };
    const auto attached=[&](uint16 parent,uint16 child) {
        HOBJECT children[2]{}; uint32 size=0,total=0;
        return scene->common().GetAttachments(objects.object(parent),children,2,size,total)==LT_OK &&
            size==1 && total==1 && children[0]==objects.object(child);
    };
    create(1); create(2); attach(1,2);
    require(attached(1,2),"server attachment fixture did not bind");
    const auto old_child=objects.object(2)->m_Lifetime;
    create(2);
    require(!*old_child && attached(1,2),"refreshing a weapon model lost its player's attachment");
    require(scene->position(objects.object(1),{10,20,30},true)==LT_OK &&
        objects.object(2)->m_Pos.DistSqr({12,23,34})<.001f,"refreshed weapon model stayed at its old position");
    const auto old_parent=objects.object(1)->m_Lifetime;
    nolf::ClientSceneEvents callbacks; callbacks.removed=[&](HOBJECT object) { objects.forget(object); };
    scene->events(callbacks); scene->client_flags(objects.object(1),CF_NOTIFYREMOVE);
    create(1);
    require(!*old_parent && attached(1,2) && objects.object(2)->m_Pos.DistSqr({2,3,4})<.001f,
        "refreshing a character lost its weapon attachment");
    Bytes empty; empty.u8(0x80).u8(1).u16(1).u16(0xffff);
    objects.apply(8,empty.data,files);
    create(2);
    require(!attached(1,2),"a replacement attachment list resurrected its old weapon");
    attach(1,2);
    Bytes remove; remove.u8(0).u8(3).u16(2);
    objects.apply(8,remove.data,files); create(2);
    require(attached(1,2),"a weapon returning to the visible set lost its player's attachment");
    remove={}; remove.u8(0).u8(3).u16(1);
    objects.apply(8,remove.data,files); create(1);
    require(!attached(1,2),"a new parent inherited an obsolete attachment list");
    attach(1,3); create(1); create(3);
    require(attached(1,3),"late weapon creation lost its pending attachment");
}
}
int main() { try { checks(); std::cout<<"Native world sync tracked sounds play and stop on removal and on reset\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
