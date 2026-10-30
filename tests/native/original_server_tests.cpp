#include "engine/server/servermgr.h"
#include "engine/server/servermsg.h"
#include "engine/portable/engine_resources.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/file_stream.h"
#include "engine/portable/saved_game.h"
#include "engine/server/stringbank.h"
#include "game/Shared/TriggerMessage.h"
#include "game/Shared/DamageTypes.h"
#include "game/ObjectDLL/ObjectMsgs.h"
#include <iostream>
#include <stdexcept>
#include <charconv>
#include <cmath>
#include <array>
#include <cstring>

int main(int argc, char** argv) {
    try {
        unsigned frames = 0;
        const bool restore_save = argc == 6 && std::string_view(argv[3]) == "--restore-save";
        const bool save_restore = argc == 4 && std::string_view(argv[3]) == "--save-restore";
        const bool docks = argc == 4 && std::string_view(argv[3]) == "--docks";
        const bool flooding = argc == 4 && std::string_view(argv[3]) == "--flooding";
        const bool with_player = docks || (argc == 5 && std::string_view(argv[3]) == "--player-frames");
        const bool cinematics = argc == 4 && std::string_view(argv[3]) == "--cinematics";
        if (argc == 5 && (std::string_view(argv[3]) == "--frames" || with_player)) {
            const std::string_view value(argv[4]);
            const auto parsed = std::from_chars(value.data(),value.data()+value.size(),frames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !frames || frames > 36000)
                throw std::runtime_error("Expected 1..36000 simulation frames");
        } else if (!restore_save && (argc < 3 || argc > 4 || (argc == 4 && !save_restore && !docks && !flooding && !cinematics && std::string_view(argv[3]) != "--full-world")))
            throw std::runtime_error("Expected native module, data directory, and optional --full-world, --cinematics, --flooding, --docks, --save-restore, --restore-save WORLD FILE, --frames COUNT or --player-frames COUNT");
        const auto resources = std::make_shared<nolf::Resources>(argv[2]);
        nolf::configure_engine_resources(resources);
        struct ResetResources { ~ResetResources() { nolf::configure_engine_resources(nullptr); } } reset;
        CServerMgr server;
        if (!server.Init()) throw std::runtime_error("Original game server initialization failed");
        if (restore_save || save_restore) {
            server.m_FileMgr.mount_game(argv[2]);
            server.m_ClassMgr.configure(argv[1], {resources.get(), "cres", 1033});
            if (server.m_ClassMgr.load(server.m_pUnknown928) != LT_OK) throw std::runtime_error(server.m_ClassMgr.error());
            std::string world = restore_save ? argv[4] : "worlds/m07s01";
            std::vector<std::byte> manual_save;
            if (save_restore) {
                if (server.DoStartWorld(world.data(),3,0) != LT_OK) throw std::runtime_error("Save fixture world startup failed");
                for (unsigned frame=1;frame<=600;++frame)
                    if (!server.Update(0,float(frame)/60)) throw std::runtime_error("Save fixture simulation failed");
                std::vector<ObjectLink> links;
                for (const auto& [object,owner] : server.m_Unknown390.m_Allocated)
                    if (std::string_view(object->m_pSData->m_pClass->m_ClassName) == "WorldProperties") links.push_back({object,nullptr});
                if (links.size() != 1) throw std::runtime_error("Save fixture requires WorldProperties first");
                for (const auto& [object,owner] : server.m_Unknown390.m_Allocated)
                    if (object != links.front().m_hObject) links.push_back({object,nullptr});
                for (std::size_t i=1;i<links.size();++i) links[i-1].m_pNext=&links[i];
                ObjectList list{links.data(),static_cast<uint32>(links.size())};
                nolf::MemoryStream output({},true);
                if (sav_SaveObjects(&server,&output,&list,3,3) != LT_OK) throw std::runtime_error("Mission save after camera simulation failed");
                manual_save.assign(output.data().begin(),output.data().end());
            }
            uint32 id_count = 0;
            for (unsigned round=0;round<3;++round) {
                if (server.DoStartWorld(world.data(),4,0) != LT_OK) throw std::runtime_error("Restore world startup failed");
                nolf::MemoryStream memory(manual_save);
                std::unique_ptr<nolf::FileStream> file(restore_save ? nolf::OpenFileStream(argv[5],nolf::platform::FileAccess::Read) : nullptr);
                ILTStream* input = restore_save ? static_cast<ILTStream*>(file.get()) : &memory;
                if (!input || sav_RestoreObjects(&server,input,3,RESTOREOBJECTS_RESTORETIME) != LT_OK)
                    throw std::runtime_error("Original saved game restore failed");
                sm_DrainObjectQueue(&server);
                // Old startup saves can contain a nonfinite scan rate even
                // though their engine rotation was saved before the first tick.
                server.m_FrameTime = 1.0f/60;
                for (const auto& [object,owner] : server.m_Unknown390.m_Allocated) {
                    if (std::string_view(object->m_pSData->m_pClass->m_ClassName) != "SecurityCamera") continue;
                    for (unsigned tick=0;tick<600;++tick)
                        server.m_ClassMgr.message(object->m_pSData->m_pBaseClass,MID_UPDATE,nullptr,0);
                    for (auto component : object->m_Rotation.m_Quat)
                        if (!std::isfinite(component)) throw std::runtime_error("Restored security camera poisoned its rotation");
                }
                if (round && server.m_Unknown8bc != id_count) throw std::runtime_error("Repeated restore leaked object IDs");
                id_count = server.m_Unknown8bc;
                std::cout << "Original saved game restored with " << server.m_Unknown390.m_Allocated.size() << " actors, " << id_count << " IDs\n";
            }
            server.Term();
            return 0;
        }
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
            std::string level = flooding ? "worlds/m05s05" : docks ? "worlds/m05s02" : cinematics ? "worlds/m03s01" : "worlds/m01s01";
            if (server.DoStartWorld(level.data(),3,0) != LT_OK) throw std::runtime_error("Original full-world startup failed");
            std::cout << "Original " << level << " started with " << server.m_Unknown390.m_Allocated.size() << " live native game actors\n";
            if (flooding) {
                auto* api=server.m_pUnknown928;
                const auto find=[&](const char* name) {
                    ObjArray<HOBJECT,1> found;
                    api->FindNamedObjects(const_cast<char*>(name),found,nullptr);
                    if (found.NumObjects()!=1) throw std::runtime_error(std::string("Flooding actor missing: ")+name);
                    return found.GetObject(0);
                };
                unsigned frame=0;
                const auto advance=[&](unsigned count) { while (count--) { ++frame; if (!server.Update(0,float(frame)/60)) throw std::runtime_error("Flooding simulation stopped"); } };
                const auto trigger=[&](const char* name,const char* command) {
                    auto* message=api->StartMessageToObject(nullptr,find(name),1000);
                    if (!message || WriteTriggerMessage(message,command)!=LT_OK) throw std::runtime_error("Flooding trigger failed");
                    api->EndMessage(message);
                };
                advance(120);
                auto* water=find("Water2"); const auto initial=water->m_Pos;
                trigger("h2oKF","ON"); advance(300);
                const auto risen=water->m_Pos;
                if (risen.y<initial.y+50) throw std::runtime_error("Ship water did not rise");
                trigger("h2oKF","PAUSE"); advance(300);
                if (water->m_Pos.DistSqr(risen)>.0001f) throw std::runtime_error("Paused ship water kept rising");
                trigger("h2oKF","RESUME"); advance(1800);
                const auto final=water->m_Pos; advance(600);
                // The compiled volume's centre differs from its authored
                // key position. Its path rises 720 - 112 world units.
                if (std::abs((final.y-initial.y)-608)>1 || water->m_Pos.DistSqr(final)>.0001f)
                    throw std::runtime_error("Ship water failed to stop at its authored final level");
                std::cout << "Ship water at " << initial.x << ',' << initial.z << " rises, pauses, resumes and stops: " << initial.y << " -> " << risen.y << " -> " << final.y << '\n';
            }
            if (cinematics) {
                // A completed line leaves m_hLastSpeaker for cleanup. The
                // m03s01 ending removes hero2 before switching its cameras off.
                auto* api=server.m_pUnknown928;
                ObjArray<HOBJECT,1> speakers;
                char speaker_name[]="hero2";
                api->FindNamedObjects(speaker_name,speakers,nullptr);
                if (speakers.NumObjects()!=1) throw std::runtime_error("Airplane cinematic speaker missing");
                auto* speaker=speakers.GetObject(0);
                ObjectCreateStruct create;
                std::snprintf(create.m_Name,sizeof create.m_Name,"CinematicLinkRegression");
                auto properties=std::make_shared<ObjectProperties>(
                    "CreateCamera 0; CreateKeyFramer 0; OneTimeOnly 0; StartOn 1; "
                    "Dialogue1 9000; WhoPlaysDialogue1 hero2; "
                    "Delay2 1000; Dialogue2 9000; WhoPlaysDialogue2 hero2;");
                auto* cinematic=server.CreateObjectWithProperties(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("CinematicTrigger")),
                    &create,std::move(properties),PRECREATE_STRINGPROP);
                if (!cinematic) throw std::runtime_error("Cinematic link fixture creation failed");
                for (unsigned frame=1;frame<=60;++frame)
                    if (!server.Update(0,float(frame)/60)) throw std::runtime_error("Cinematic fixture update failed");
                bool retained=false;
                auto* head=&speaker->m_pSData->m_Links;
                for (auto* node=head->m_pNext;node!=head;node=node->m_pNext) {
                    auto* link=static_cast<InterObjectLink*>(node->m_pData);
                    if (link->m_Type==0 && link->m_pOwner==cinematic->m_hObject && link->m_pLinked==speaker) retained=true;
                }
                if (!retained) throw std::runtime_error("Completed cinematic line dropped its last speaker's removal notification");
                for (unsigned round=0;round<2;++round) {
                    // The dialogue manager must have its own engine identity
                    // and accept another window after its cinematic is removed.
                    auto window_properties=std::make_shared<ObjectProperties>(
                        "CreateCamera 0; CreateKeyFramer 0; OneTimeOnly 0; StartOn 1; "
                        "Dialogue1 9000; WhoPlaysDialogue1 hero2; Decisions1 \"10670;10674\";");
                    auto* window=server.CreateObjectWithProperties(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("CinematicTrigger")),
                        &create,std::move(window_properties),PRECREATE_STRINGPROP);
                    if (!window || !server.Update(0,float(61+round)/60)) throw std::runtime_error("Windowed dialogue fixture failed");
                    bool linked=false;
                    auto* links=&window->m_hObject->m_pSData->m_Links;
                    for (auto* node=links->m_pNext;node!=links;node=node->m_pNext) {
                        auto* link=static_cast<InterObjectLink*>(node->m_pData);
                        if (link->m_Type==0 && link->m_pLinked==window->m_hObject &&
                            std::string_view(link->m_pOwner->m_pSData->m_pClass->m_ClassName)=="CDialogueWindow") linked=true;
                    }
                    if (!linked) throw std::runtime_error("Dialogue window has no engine lifetime link, or retained a removed cinematic");
                    if (sm_RemoveObjectFromWorld(&server,window)!=LT_OK) throw std::runtime_error("Windowed cinematic removal failed");
                }
                if (sm_RemoveObjectFromWorld(&server,speaker->m_pSData->m_pBaseClass)!=LT_OK)
                    throw std::runtime_error("Cinematic speaker removal failed");
                auto* message=api->StartMessageToObject(nullptr,cinematic->m_hObject,1000);
                if (!message || WriteTriggerMessage(message,"OFF")!=LT_OK) throw std::runtime_error("Cinematic cleanup message failed");
                api->EndMessage(message);
                std::cout << "Airplane cinematic retains a completed speaker's removal link and cleans up after actor removal\n";
            }
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
            if (docks) {
                auto* api=server.m_pUnknown928;
                // The real client performs this startup autosave before AI
                // begins processing its scripted command queue.
                std::vector<ObjectLink> startup_links; startup_links.reserve(server.m_Unknown390.m_Allocated.size());
                for (auto& [object,owner] : server.m_Unknown390.m_Allocated) startup_links.push_back({object,nullptr});
                for (unsigned i=1;i<startup_links.size();++i) startup_links[i-1].m_pNext=&startup_links[i];
                ObjectList player_list{startup_links.data(),static_cast<uint32>(startup_links.size())};
                nolf::MemoryStream autosave({},true);
                if (sav_SaveObjects(&server,&autosave,&player_list,93,0)!=LT_OK) throw std::runtime_error("Dock startup autosave failed");
                const auto find=[&](const char* name) {
                    ObjArray<HOBJECT,1> found;
                    api->FindNamedObjects(const_cast<char*>(name),found,nullptr);
                    if (found.NumObjects()!=1) throw std::runtime_error(std::string("Dock fixture actor missing: ")+name);
                    return found.GetObject(0);
                };
                for (unsigned frame=1;frame<=120;++frame) server.Update(0,float(frame)/60);
                std::array<LTVector,3> before;
                const std::array<const char*,3> names{"BG10","BG11","BG12"};
                const std::array<const char*,3> covers{"Ncover4","Ncover3","Ncover5"};
                for (unsigned i=0;i<3;++i) {
                    auto* guard=find(names[i]);
                    for (const auto& command : {std::string("TELEPORT ")+names[i]+"TP",std::string("SENSES 1"),std::string("TARGETPLAYER"),std::string("ATTACKFROMCOVER DEST=")+covers[i]}) {
                        auto* message=api->StartMessageToObject(nullptr,guard,1000);
                        if (!message || WriteTriggerMessage(message,command.c_str())!=LT_OK) throw std::runtime_error("Dock guard trigger failed");
                        api->EndMessage(message);
                    }
                }
                for (unsigned i=0;i<3;++i) before[i]=find(names[i])->m_Pos;
                for (unsigned frame=121;frame<=420;++frame) server.Update(0,float(frame)/60);
                for (unsigned i=0;i<3;++i)
                    if (find(names[i])->m_Pos.Dist(before[i])<16) throw std::runtime_error(std::string("Teleported dock guard failed to follow cover path: ")+names[i]);
                std::cout << "Teleported dock guards follow their scripted cover paths\n";
                // Restoring these objects constructs default HSTRINGs before
                // MID_LOADOBJECT replaces them with the saved values.
                const auto count=[] {
                    unsigned result=0;
                    sb_EnumThreadStrings(+[](char* text,void* context) {
                        const std::string_view s(text);
                        if (s.starts_with("AI_NEUTRAL_") || s=="AI_GOOD_MaleBystander" || s=="Powerups\\Snd\\Pickup.wav" || s=="Powerups\\Snd\\Respawn.wav")
                            ++*static_cast<unsigned*>(context);
                    },&result);
                    return result;
                };
                for (const char* cls : {"AI_NEUTRAL_EliteAsian","AI_NEUTRAL_EliteWhite","AI_NEUTRAL_EliteBlack","AI_NEUTRAL_UNITYCouncil","AI_NEUTRAL_Hero","AI_GOOD_MaleBystander","AmmoBox"}) {
                    const auto baseline=count();
                    ObjectCreateStruct create; std::strcpy(create.m_Name,"RestoreStringFixture");
                    auto* actor=server.CreateObjectWithProperties(reinterpret_cast<HCLASS>(server.m_ClassMgr.find(cls)),&create,
                        std::make_shared<ObjectProperties>(""),PRECREATE_STRINGPROP);
                    if (!actor) throw std::runtime_error(std::string("Restore fixture creation failed: ")+cls);
                    ObjectLink link{actor->m_hObject,nullptr}; ObjectList list{&link,1}; nolf::MemoryStream saved({},true);
                    if (sav_SaveObjects(&server,&saved,&list,93,0)!=LT_OK || sm_RemoveObjectFromWorld(&server,actor)!=LT_OK ||
                        saved.SeekTo(0)!=LT_OK || sav_RestoreObjects(&server,&saved,93,0)!=LT_OK)
                        throw std::runtime_error(std::string("Original restore fixture failed: ")+cls);
                    auto* restored=find("RestoreStringFixture");
                    if (sm_RemoveObjectFromWorld(&server,restored->m_pSData->m_pBaseClass)!=LT_OK || count()!=baseline)
                        throw std::runtime_error(std::string("Restore leaked constructor-owned strings: ")+cls);
                }
                std::cout << "Original AI and pickup save/restore releases constructor-owned strings\n";
                auto* player=player_client->m_pUnknown3AC;
                auto* damage=api->StartMessageToObject(player->m_pSData->m_pBaseClass,find("BG10"),MID_DAMAGE);
                LTVector direction{0,0,1}; api->WriteToMessageVector(damage,&direction);
                api->WriteToMessageFloat(damage,0); api->WriteToMessageFloat(damage,200);
                api->WriteToMessageByte(damage,DT_BULLET); api->WriteToMessageObject(damage,player); api->WriteToMessageObject(damage,nullptr);
                api->EndMessage(damage);
                for (unsigned frame=421;frame<=660;++frame) server.Update(0,float(frame)/60);
                bool remembered=false; auto* head=&player->m_pSData->m_Links;
                for (auto* node=head->m_pNext;node!=head;node=node->m_pNext) {
                    auto* link=static_cast<InterObjectLink*>(node->m_pData);
                    if (link->m_Type==0 && link->m_pLinked==player && std::string_view(link->m_pOwner->m_pSData->m_pClass->m_ClassName)=="Body") remembered=true;
                }
                if (!remembered) throw std::runtime_error("Corpse lost the original character's attacker for AI death reactions");
                std::cout << "Corpses retain the attacker's removal link for AI death reactions\n";
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
