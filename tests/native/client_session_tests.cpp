#include "engine/client/client_session.h"
#include "engine/client/client_transport.h"
#include "engine/client/game_handshake.h"
#include "engine/portable/compatibility.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/binary.h"
#include "engine/server/world_state.h"
#include "engine/server/model_cache.h"
#include "engine/portable/wave.h"
#include "game/Shared/NetDefs.h"
#include "game/Shared/MsgIDs.h"
#include "game/Shared/ClientServerShared.h"
#include "platform/system.h"
#include "platform/timing.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <random>
extern int g_NetDebugLevel;

namespace {
using nolf::PacketBytes;
using Kind = nolf::ClientEvent::Kind;
using Phase = nolf::ClientSession::Phase;
void require(bool result,const char* why) { if (!result) throw std::runtime_error(why); }
template<class F> void rejects(F&& callback) {
    bool rejected = false;
    try { callback(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"expected invalid client protocol data to be rejected");
}
struct TemporaryTree {
    std::filesystem::path root = std::filesystem::temp_directory_path()/
        ("nolf-client-session-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    TemporaryTree() { std::filesystem::create_directories(root/"worlds"); }
    ~TemporaryTree() { std::error_code error; std::filesystem::remove_all(root,error); }
};
PacketBytes packet(unsigned id,const nolf::BinaryWriter& data) {
    PacketBytes bytes{std::byte(id)};
    bytes.insert(bytes.end(),data.data().begin(),data.data().end()); return bytes;
}
void name(nolf::BinaryWriter& output,std::string_view text) {
    output.bytes(std::as_bytes(std::span(text))); output.integer<std::uint8_t>(0);
}
void file(nolf::BinaryWriter& output,std::uint16_t id,std::uint32_t size,std::string_view text) {
    output.integer(id); output.integer(size); name(output,text);
}
void protocol() {
    TemporaryTree tree;
    const PacketBytes expected{std::byte{1},std::byte{2},std::byte{3}};
    nolf::platform::atomic_write(tree.root/"worlds/test.dat",expected);
    auto assets = std::make_shared<nolf::Assets>(); assets->mount_directory(tree.root);
    nolf::ServerFiles files(assets);
    nolf::BinaryWriter announcement;
    file(announcement,7,3,"WORLDS\\TEST.DAT");
    auto replies = files.announce(announcement.data());
    require(replies.size() == 1 && replies[0] == PacketBytes{std::byte{0x36},std::byte{7},std::byte{0x80}},"file acknowledgement uses original fixed-width ID framing");
    require(files.require(7).name == "worlds/test.dat" && files.size() == 1,"server ID resolves owned local asset source");
    for (std::size_t length = 1; length < announcement.data().size(); ++length) {
        nolf::ServerFiles truncated(assets);
        rejects([&] { truncated.announce(std::span(announcement.data()).first(length)); });
        require(!truncated.size(),"truncated announcement cannot publish a partial catalogue");
    }
    nolf::BinaryWriter invalid;
    file(invalid,8,3,"worlds/test.dat"); file(invalid,7,3,"different.dat");
    rejects([&] { files.announce(invalid.data()); });
    require(files.size() == 1 && !files.find(8),"conflicting server ID rolls back the whole announcement");
    nolf::BinaryWriter missing; file(missing,9,42,"missing.dat");
    files.announce(missing.data());
    require(files.find(9) && !files.find(9)->source.file,"missing asset never acquires a fabricated source");
    rejects([&] { files.require(9); });
    for (auto path : {"../private","/private","C:\\private"}) {
        nolf::BinaryWriter value; file(value,10,3,path);
        rejects([&] { files.announce(value.data()); });
    }
    nolf::BinaryWriter mismatch; file(mismatch,10,4,"worlds/test.dat");
    rejects([&] { files.announce(mismatch.data()); });

    nolf::ClientSession session(assets);
    rejects([&] { session.receive({}); });
    session.begin(expected);
    require(session.take_outgoing() == std::vector<PacketBytes>{{std::byte{5},std::byte{3},std::byte{0},std::byte{1},std::byte{2},std::byte{3}}},"client data has explicit LE length and owned payload");
    nolf::BinaryWriter identity; identity.integer<std::uint16_t>(17); identity.integer<std::uint8_t>(0);
    rejects([&] { session.receive(packet(12,identity)); });
    nolf::BinaryWriter version; version.integer<std::uint32_t>(6);
    session.receive(packet(4,version)); session.receive(packet(12,identity));
    require(session.version_verified() && session.client_id() == 17 && !session.local(),"real engine version and client identity are checked");
    session.take_events();
    session.receive(packet(0x32,announcement)); session.take_outgoing();
    session.receive(PacketBytes{std::byte{5}}); session.take_events();
    nolf::BinaryWriter world; world.real(1.25f); world.integer<std::uint16_t>(7);
    session.receive(packet(6,world));
    auto events = session.take_events();
    require(events.size() == 1 && events[0].kind == Kind::LoadWorld && events[0].name == "worlds/test.dat" &&
        events[0].server_time == 1.25f && session.take_outgoing().empty(),"world request produces owned work before any readiness acknowledgement");
    rejects([&] { session.preload_complete(); });
    session.world_loaded();
    require(session.take_outgoing() == std::vector<PacketBytes>{{std::byte{9},std::byte{0}}},"world completion emits first handshake stage");
    session.receive(PacketBytes{std::byte{21},std::byte{0}});
    rejects([&] { session.preload_complete(); });
    rejects([&] { session.receive(PacketBytes{std::byte{21},std::byte{2},std::byte{7}}); });
    session.receive(PacketBytes{std::byte{21},std::byte{3},std::byte{7},std::byte{0}});
    session.receive(PacketBytes{std::byte{21},std::byte{1}});
    require(session.take_outgoing().empty(),"preload boundary does not acknowledge unfinished application work");
    events = session.take_events();
    require(events.size() == 3 && events[0].kind == Kind::PreloadBegin && events[1].files == std::vector<std::uint16_t>{7} &&
        events[2].kind == Kind::PreloadEnd,"preload events retain their type and server IDs");
    session.preload_complete();
    require(session.take_outgoing() == std::vector<PacketBytes>{{std::byte{9},std::byte{1}}},"preload completion emits second engine stage");
    session.receive(PacketBytes{std::byte{7},std::byte{42},std::byte{0}});
    require(session.object_id() == 42 && session.phase() == Phase::InWorld,"server player assignment advances engine session");
    session.send_commands(30,std::array<std::uint8_t,4>{254,2,0,2},std::array<std::uint16_t,1>{0x1234});
    require(session.take_outgoing() == std::vector<PacketBytes>{{std::byte{7},std::byte{30},std::byte{3},std::byte{0},std::byte{2},std::byte{254},std::byte{0x34},std::byte{0x12}}},
        "client input emits complete active commands and fixed-width sound completions");
    rejects([&] { session.send_commands(30,std::array<std::uint8_t,1>{255}); });
    session.take_events();
    session.receive(PacketBytes{std::byte{21},std::byte{2},std::byte{7},std::byte{0}});
    auto incremental = session.take_events();
    require(incremental.size() == 1 && incremental[0].kind == Kind::PreloadFiles && incremental[0].files == std::vector<std::uint16_t>{7} &&
        session.phase() == Phase::InWorld,"runtime resource requests do not restart or acknowledge initial preload negotiation");
    // Update message groups share framing with the standalone group packet.
    // The object-state tail is retained for the client object decoder.
    const PacketBytes grouped{std::byte{8},std::byte{4},std::byte{13},std::byte{10},std::byte{20},std::byte{245},
        std::byte{0},std::byte{123},std::byte{45}};
    session.receive(grouped);
    events = session.take_events();
    require(events.size() == 2 && events[0].kind == Kind::GameMessage && events[0].id == 245 &&
        events[0].bytes == PacketBytes{std::byte{10},std::byte{20}} && events[1].bytes == PacketBytes{std::byte{123},std::byte{45}},
        "grouped game messages and object-state tail remain independent owned events");
    session.send_game(245,expected);
    require(session.take_outgoing() == std::vector<PacketBytes>{{std::byte{11},std::byte{1},std::byte{2},std::byte{3},std::byte{245}}},"game message ID follows payload on the original wire");
    rejects([&] { session.receive(PacketBytes{std::byte{14},std::byte{3},std::byte{13}}); });
    PacketBytes nested{std::byte{13},std::byte{1}};
    for (unsigned depth = 0; depth < 10; ++depth) {
        PacketBytes outer{std::byte{14},std::byte(nested.size())};
        outer.insert(outer.end(),nested.begin(),nested.end()); nested = std::move(outer);
    }
    rejects([&] { session.receive(nested); });
    session.close();
    require(session.phase() == Phase::Closed && !session.client_id() && !session.object_id() && session.files().size() == 0,
        "connection closure retires catalogue, IDs, queued work and replies");
    rejects([&] { session.receive(grouped); });
    // A local listen server can send its complete entry sequence before the
    // application has finished loading the world. It still must not become
    // ready until the application has completed both stages.
    nolf::ClientSession local(assets);
    local.begin({}); local.take_outgoing(); local.receive(packet(4,version));
    nolf::BinaryWriter local_identity; local_identity.integer<std::uint16_t>(1); local_identity.integer<std::uint8_t>(1);
    local.receive(packet(12,local_identity)); local.receive(packet(0x32,announcement)); local.take_outgoing();
    local.receive(packet(6,world));
    local.receive(PacketBytes{std::byte{21},std::byte{0}}); local.receive(PacketBytes{std::byte{21},std::byte{1}});
    local.receive(PacketBytes{std::byte{7},std::byte{50},std::byte{0}});
    require(local.phase() == Phase::LoadingWorld && local.object_id() == 50 && local.take_outgoing().empty(),
        "early local entry cannot bypass actual application loading");
    local.world_loaded(); local.preload_complete();
    require(local.phase() == Phase::InWorld,"local entry completes after application world and preload acknowledgements");
    assets.reset(); files.clear();
    std::cout << "native client catalogue, handshake, staged readiness and grouped messages passed\n";
}
void authentication() {
    TemporaryTree tree;
    const std::string known = "123456789";
    nolf::platform::atomic_write(tree.root/"crc.txt",std::as_bytes(std::span(known)));
    nolf::Assets assets; assets.mount_directory(tree.root);
    require(nolf::asset_crc32(assets.locate("crc.txt")) == 0xcbf43926u,"CRC matches standard independent vector");
    nolf::BinaryWriter identity;
    const std::string magic = "NLFCMP01";
    identity.bytes(std::as_bytes(std::span(magic))); identity.integer<std::uint32_t>(100); identity.integer<std::uint32_t>(0x12345678);
    for (unsigned length=0; length<16; ++length) rejects([&] { nolf::read_compatibility_identity(std::span(identity.data()).first(length)); });
    require(nolf::read_compatibility_identity(identity.data()).crc32 == 0x12345678,"retail identity reads explicit little endian CRC");
    rejects([&] { nolf::client_shell_compatibility_crc(assets); });
    std::filesystem::create_directories(tree.root/"native/compatibility");
    nolf::platform::atomic_write(tree.root/"native/compatibility/cshell.dll",identity.data());
    nolf::platform::atomic_write(tree.root/"cshell.dll",PacketBytes(100));
    nolf::Assets imported; imported.mount_directory(tree.root);
    require(nolf::client_shell_compatibility_crc(imported) == 0x12345678,"stripped executable retains separately imported retail wire identity");
    nolf::platform::atomic_write(tree.root/"cshell.dll",PacketBytes(101));
    rejects([&] { nolf::client_shell_compatibility_crc(imported); });
    // Independent server-side challenge and password equations, including a
    // zero rotation (the old macro shifted by 32) and short/non-ASCII names.
    for (std::string player : {std::string("NativeClient"),std::string("P"),std::string("abc"),std::string("\x80z")}) {
        constexpr std::uint32_t nonce=0x9abcdef0, server_mask=0x13579bdf, weapons=0x2468ace0, shell=0x12345678;
        nolf::GameHandshake handshake(player,nonce,weapons,shell);
        nolf::BinaryWriter hello; hello.integer<std::uint8_t>(MID_HANDSHAKE_HELLO); hello.integer<std::uint16_t>(GAME_HANDSHAKE_VER);
        for (std::size_t size=0; size<hello.data().size(); ++size) rejects([&] { handshake.receive(std::span(hello.data()).first(size)); });
        auto reply=handshake.receive(hello.data()); require(bool(reply),"hello has client response");
        nolf::BinaryReader greeting(*reply);
        require(greeting.integer<std::uint8_t>() == 0 && greeting.integer<std::uint16_t>() == GAME_HANDSHAKE_VER && greeting.integer<std::uint32_t>() == nonce && !greeting.remaining(),"client hello retains explicit nonce and version");
        const auto mask=GAME_HANDSHAKE_ROTATE(static_cast<unsigned char>(player[0]) & 15) ^ server_mask;
        nolf::BinaryWriter challenge; challenge.integer<std::uint8_t>(MID_HANDSHAKE_PASSWORD); challenge.integer<std::uint32_t>(mask ^ nonce);
        reply=handshake.receive(challenge.data()); nolf::BinaryReader answer(*reply);
        std::uint32_t name_key=0;
        for (unsigned i=0; i<4 && i<player.size(); ++i) name_key |= std::uint32_t(static_cast<unsigned char>(player[i])) << (8*i);
        require(answer.integer<std::uint8_t>() == MID_HANDSHAKE_LETMEIN && answer.integer<std::uint32_t>() == (mask ^ name_key) &&
            answer.integer<std::uint32_t>() == (mask ^ weapons) && answer.integer<std::uint32_t>() == (mask ^ shell) && !answer.remaining(),"GOTY password and both CRCs match independent server equations");
        rejects([&] { handshake.receive(challenge.data()); });
        require(!handshake.receive(PacketBytes{std::byte{MID_HANDSHAKE_DONE}}) && handshake.phase() == nolf::GameHandshake::Phase::Complete,"only server confirmation authenticates game session");
    }
    nolf::GameHandshake bad("Player",1,2,3);
    rejects([&] { bad.receive(PacketBytes{std::byte{0},std::byte{4},std::byte{1}}); });
    require(bad.phase() == nolf::GameHandshake::Phase::Hello,"bad version cannot advance authentication");
}
void original_peer(std::string_view address,const std::filesystem::path& data,bool trace,std::string_view reject_mode) {
    std::cout << std::unitbuf;
    if (trace) g_NetDebugLevel = 2;
    auto assets = std::make_shared<nolf::Assets>(); assets->mount_game(data);
    nolf::ClientSession session(assets);
    nolf::ClientTransport transport(kNolfApplicationGuid);
    require(transport.connect(address) == LT_OK,"native client connects to original-game host transport");
    NetClientData client{};
    const std::string player_name = "NativeClient";
    nolf::GameHandshake handshake(player_name,std::random_device{}(),nolf::asset_crc32(assets->locate("Attributes/Weapons.txt")) ^ (reject_mode == "--bad-weapon"),
        nolf::client_shell_compatibility_crc(*assets) ^ (reject_mode == "--bad-shell"));
    std::copy(player_name.begin(),player_name.end(),client.m_sName);
    nolf::BinaryWriter client_data;
    client_data.bytes(std::as_bytes(std::span(client.m_sName))); client_data.integer<std::uint32_t>(TEAM_AUTO);
    session.begin(client_data.data());
    CServerWorld world;
    ModelCache models(assets);
    std::vector<std::shared_ptr<Model>> retained_models;
    std::vector<nolf::AssetSource> retained_assets;
    std::size_t model_count = 0, wave_count = 0, game_messages = 0, update_bytes = 0, sky_updates = 0;
    bool game_hello = false;
    std::array<unsigned,64> packet_counts{};
    unsigned entered_frames = 0;
    const auto start = nolf::platform::Clock::now();
    nolf::platform::FrameLimiter limiter(60);
    while (nolf::platform::Clock::now()-start < std::chrono::seconds(30)) {
        for (const auto& outgoing : session.take_outgoing()) transport.send(outgoing);
        const auto time = std::chrono::duration<float>(nolf::platform::Clock::now()-start).count();
        for (const auto& incoming : transport.pump(time)) {
            ++packet_counts[std::to_integer<unsigned>(incoming[0]) & 63];
            session.receive(incoming);
        }
        require(transport.connected(),"original host disconnected native protocol peer");
        for (const auto& event : session.take_events()) {
            if (event.kind == Kind::UnloadWorld) { world.Term(); retained_models.clear(); retained_assets.clear(); }
            else if (event.kind == Kind::LoadWorld) {
                nolf::AssetStream stream(session.files().require(event.files.at(0)).source);
                WorldLoadRequest request; request.m_pStream = &stream;
                require(world.Load(&request) == 0 && world.LoadObjects(&stream) == LT_OK,"client loads actual original world geometry and light animations");
                std::cout << "Client world loaded: " << event.name << "; " << world.m_Worlds.GetSize() << " models\n";
                session.world_loaded();
            } else if (event.kind == Kind::PreloadBegin) { retained_models.clear(); retained_assets.clear(); }
            else if (event.kind == Kind::PreloadFiles) {
                for (auto id : event.files) {
                    const auto& file = session.files().require(id);
                    if (event.id == 2) {
                        retained_models.push_back(models.Load(file.name)); ++model_count;
                    } else if (event.id == 5) { nolf::read_wave(file.source); ++wave_count; }
                    retained_assets.push_back(file.source);
                }
            } else if (event.kind == Kind::PreloadEnd) session.preload_complete();
            else if (event.kind == Kind::ClientObject) {
                std::cout << "Client object assigned: " << event.object_id << '\n';
                session.send_commands(30,{});
            } else if (event.kind == Kind::GameMessage) {
                ++game_messages;
                if (game_messages < 8) std::cout << "Game message " << unsigned(event.id) << ": " << event.bytes.size() << " bytes\n";
                if (event.id == MID_HANDSHAKE) {
                    if (auto reply=handshake.receive(event.bytes)) session.send_game(MID_HANDSHAKE,*reply);
                    game_hello = handshake.phase() == nolf::GameHandshake::Phase::Complete;
                }
            } else if (event.kind == Kind::Sky) { require(event.sky && nolf::valid_sky(event.sky->definition),"original host sent an invalid typed sky definition"); ++sky_updates; }
            else if (event.kind == Kind::EnginePacket) update_bytes += event.bytes.size();
        }
        if (session.phase() == Phase::InWorld && game_hello && ++entered_frames == 120) {
            require(sky_updates>0,"original host never delivered its sky state");
            std::cout << "Native engine peer entered: client " << *session.client_id() << ", player object " << *session.object_id()
                << ", " << model_count << " models, " << wave_count << " waves, " << game_messages << " game messages, "
                << update_bytes << " engine-state bytes, " << sky_updates << " typed sky updates; game authentication confirmed\n";
            transport.disconnect(); session.close(); return;
        }
        limiter.wait();
    }
    std::cerr << "Session phase " << unsigned(session.phase()) << ", game hello " << game_hello << ", messages " << game_messages << "; packets";
    for (unsigned id = 0; id < packet_counts.size(); ++id) if (packet_counts[id]) std::cerr << ' ' << id << ':' << packet_counts[id];
    std::cerr << '\n';
    throw std::runtime_error("original host engine handshake timed out");
}
}
int main(int argc,char** argv) {
    try {
        if (argc == 1) { protocol(); authentication(); }
        else if (argc >= 5 && argc <= 7 && std::string_view(argv[1]) == "--connect" && std::string_view(argv[3]) == "--data") {
            bool trace=false; std::string_view reject_mode;
            for (int i=5; i<argc; ++i) {
                const std::string_view option(argv[i]);
                if (option == "--trace" && !trace) trace=true;
                else if ((option == "--bad-weapon" || option == "--bad-shell") && reject_mode.empty()) reject_mode=option;
                else throw std::runtime_error("Unknown or duplicate peer option");
            }
            original_peer(argv[2],argv[4],trace,reject_mode);
        } else throw std::runtime_error("Expected optional --connect ADDRESS --data DIRECTORY [--trace] [--bad-weapon|--bad-shell]");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
