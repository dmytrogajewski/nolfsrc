#include "engine/server/servermgr.h"
#include "engine/server/servermsg.h"
#include "engine/portable/engine_resources.h"
#include "engine/portable/asset_stream.h"
#include <iostream>
#include <stdexcept>
#include <charconv>
#include <cmath>

int main(int argc, char** argv) {
    try {
        unsigned frames = 0;
        const bool with_player = argc == 5 && std::string_view(argv[3]) == "--player-frames";
        if (argc == 5 && (std::string_view(argv[3]) == "--frames" || with_player)) {
            const std::string_view value(argv[4]);
            const auto parsed = std::from_chars(value.data(),value.data()+value.size(),frames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !frames || frames > 36000)
                throw std::runtime_error("Expected 1..36000 simulation frames");
        } else if (argc < 3 || argc > 4 || (argc == 4 && std::string_view(argv[3]) != "--full-world"))
            throw std::runtime_error("Expected native module, data directory, and optional --full-world, --frames COUNT or --player-frames COUNT");
        const auto resources = std::make_shared<nolf::Resources>(argv[2]);
        nolf::configure_engine_resources(resources);
        struct ResetResources { ~ResetResources() { nolf::configure_engine_resources(nullptr); } } reset;
        CServerMgr server;
        if (!server.Init()) throw std::runtime_error("Original game server initialization failed");
        server.m_ClassMgr.configure(argv[1]);
        if (server.m_ClassMgr.load(server.m_pUnknown928) != LT_INVALIDOBJECTDLL || server.m_ClassMgr.m_pServerShell)
            throw std::runtime_error("Missing original game attributes must reject shell startup");
        server.Term();
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            if (!server.Init()) throw std::runtime_error("Original game server restart failed");
            server.m_FileMgr.mount_game(argv[2]);
            server.m_ClassMgr.configure(argv[1], {resources.get(), "cres", 1033});
            if (server.m_ClassMgr.load(server.m_pUnknown928) != LT_OK)
                throw std::runtime_error(server.m_ClassMgr.error());
            if (!server.m_ClassMgr.m_pServerShell) throw std::runtime_error("Original game shell was not published");
            char invalid_message[] = "";
            if (server.m_ClassMgr.shell([&](IServerShell& shell) { return shell.ServerAppMessageFn(invalid_message,-1); }) != LT_INVALIDPARAMS)
                throw std::runtime_error("Original game shell accepted an invalid application message extent");
            const std::string level = "worlds/m01s01.dat";
            nolf::AssetStream stream(server.m_FileMgr.locate(level));
            if (server.LoadWorld(&stream, const_cast<char*>(level.c_str())) != LT_OK)
                throw std::runtime_error("Original game bootstrap level failed to load");
            bool created_start = false;
            for (const auto& authored : server.m_World.ObjectDefinitions()) {
                if (authored.class_name != "GameStartPoint") continue;
                auto properties = std::make_shared<ObjectProperties>(std::span<const WorldProperty>(authored.properties));
                const auto* position = properties->find("Pos");
                if (!position) throw std::runtime_error("Original start point has no position");
                const auto expected = position->value.m_Vec;
                ObjectCreateStruct create;
                auto* actor = server.CreateObjectWithProperties(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("GameStartPoint")),
                    &create, std::move(properties), PRECREATE_WORLDFILE);
                if (!actor || !actor->m_hObject || actor->m_hObject->m_Pos.DistSqr(expected) > .0001f)
                    throw std::runtime_error("Original start point failed native creation or lost its authored position");
                if (sm_RemoveObjectFromWorld(&server,actor) != LT_OK)
                    throw std::runtime_error("Original start point removal failed");
                created_start = true;
                break;
            }
            if (!created_start) throw std::runtime_error("Original bootstrap world has no start point");
            server.Term();
        }
        std::cout << "Original NOLF shell: missing-data rejection, three startup/shutdown cycles and authored start-point actors passed\n";
        if (argc >= 4) {
            if (!server.Init()) throw std::runtime_error("Original full-world server initialization failed");
            server.m_FileMgr.mount_game(argv[2]);
            server.m_ClassMgr.configure(argv[1], {resources.get(), "cres", 1033});
            if (server.m_ClassMgr.load(server.m_pUnknown928) != LT_OK) throw std::runtime_error(server.m_ClassMgr.error());
            char level[] = "worlds/m01s01";
            if (server.DoStartWorld(level,3,0) != LT_OK) throw std::runtime_error("Original full-world startup failed");
            std::cout << "Original m01s01 started with " << server.m_Unknown390.m_Allocated.size() << " live native game actors\n";
            Client* player_client = nullptr;
            if (with_player) {
                // This local fixture exercises original player entry and
                // simulation. It has no peer and cannot prove networking.
                auto client = std::make_unique<Client>();
                if (!client->Initialize(0xfff+40)) throw std::runtime_error("Local player fixture initialization failed");
                client->m_Name = "NativePlayer"; client->m_ClientID = 1;
                client->m_ClientFlags |= CLIENTFLAG_LOCAL; client->m_State = 2; client->m_Unknown3BC = 3;
                auto* peer = client.get();
                client->Register(server.m_ClientList); client.release();
                if (sm_UpdateClientState2(&server,peer) != LT_OK || !peer->m_pUnknown3AC)
                    throw std::runtime_error("Original local player entry failed");
                player_client = peer;
                std::cout << "Original player entered with native object " << peer->m_pUnknown3AC->m_ObjectID << '\n';
            }
            for (unsigned frame = 1; frame <= frames; ++frame)
                if (!server.Update(0,float(frame)/60)) throw std::runtime_error("Original simulation stopped at frame " + std::to_string(frame));
            if (frames) std::cout << "Original simulation: " << frames << " frames, " << server.m_Time << " game seconds, "
                << server.m_Unknown390.m_Allocated.size() << " live actors\n";
            if (player_client) {
                auto* player = player_client->m_pUnknown3AC;
                if (!player || !server.m_Unknown390.m_Allocated.contains(player) ||
                    !std::isfinite(player->m_Pos.x) || !std::isfinite(player->m_Pos.y) || !std::isfinite(player->m_Pos.z))
                    throw std::runtime_error("Original player lost its native identity or finite position");
                std::cout << "Player position: " << player->m_Pos.x << ',' << player->m_Pos.y << ',' << player->m_Pos.z
                    << "; standing contact: " << bool(player->m_pStandingOn) << '\n';
            }
            server.Term();
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
