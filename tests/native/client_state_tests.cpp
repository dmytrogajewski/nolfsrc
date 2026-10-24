#include "engine/server/client_state.h"
#include "iltserver.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>

namespace {
void require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
void packets_and_registry() {
    {
        LTList registry;
        auto first = std::make_unique<Client>();
        Client second, loading, removing;
        first->m_State = second.m_State = removing.m_State = 3;
        removing.m_Removing = true;
        first->Register(registry); second.Register(registry); loading.Register(registry); removing.Register(registry);
        auto record = std::make_shared<SoundRecord>(); record->file_name = "sound.wav"; record->file_id = 65535;
        std::weak_ptr<const SoundRecord> lifetime = record;
        require(client_QueueSound(registry,record) && first->m_QueuedSounds.size() == 1 && second.m_QueuedSounds.size() == 1 &&
            loading.m_QueuedSounds.empty() && removing.m_QueuedSounds.empty(),"sound broadcast selects live clients in world");
        record.reset(); first.reset();
        require(!lifetime.expired() && second.m_QueuedSounds[0]->file_name == "sound.wav","removing one client preserves another client's sound record");
        std::vector<std::shared_ptr<const SoundRecord>> batch;
        batch.swap(second.m_QueuedSounds);
        require(second.m_QueuedSounds.empty() && batch[0]->file_id == 65535,"draining sound batch retains typed packet metadata");
        auto later = std::make_shared<SoundRecord>(); later->file_name = "later.wav";
        require(client_QueueSound(registry,later) && second.m_QueuedSounds.size() == 1 && batch.size() == 1,"new sounds stay separate from the batch being sent");
        batch.clear(); require(lifetime.expired(),"last queue reference releases sound record");
        require(!client_QueueSound(registry,nullptr),"null sound broadcast rejected");
    }
    {
        Client client;
        auto* state = snd_FindSentState(&client,65535);
        require(state && state->m_ChangeFlags == 7 && snd_FindSentState(&client,65535) == state,"client owns stable sound state by full file ID");
        CSoundSentState next; next.m_Flags = 9; next.m_Unknown04 = 20; next.m_nRange = 123; next.m_nInnerRatio = 200;
        snd_MergeSentState(state,&next);
        require(state->m_ChangeFlags == 7 && state->m_Flags == 9 && state->m_nRange == 123,"initial sound state merged");
        state->m_ChangeFlags = 0; snd_MergeSentState(state,&next);
        require(!state->m_ChangeFlags,"unchanged sound metadata does not resend");
        ++next.m_Unknown04; snd_MergeSentState(state,&next);
        require(state->m_ChangeFlags == SNDSENT_BYTE04,"only changed sound priority marked");
        ++next.m_nInnerRatio; snd_MergeSentState(state,&next);
        require(state->m_ChangeFlags == (SNDSENT_BYTE04 | SNDSENT_RANGE),"unsent changes retained across metadata updates");
        for (uint16 i = 0; i < 1000; ++i) require(snd_FindSentState(&client,i),"owned sound states grow");
        require(snd_FindSentState(&client,65535) == state && state->m_nInnerRatio == 201,"map growth preserves existing sound state references");
    }
    LTList clients;
    CMessageRef retained[4];
    auto first = std::make_unique<Client>();
    require(first->m_AttachLink.IsTiedOff() && first->m_AttachHead.IsTiedOff() && first->m_Link.IsTiedOff(),"native client links initialize");
    require(first->Data().empty() && first->m_ObjInfos.empty() && !first->m_pViewClient && !first->m_pConnection,"native client starts without borrowed uninitialized state");
    require(!first->Initialize(UINT32_MAX) && first->m_State == 0,"bounded initial object capacity");
    require(first->Initialize(42) && first->m_ObjInfos.size() == 42 && first->m_State == 1,"client initialization commits actual owned records");
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 2; ++j) {
        auto& set = first->m_PacketSets[i][j];
        require(set.Packet() && set.Packet()->m_nRefCount == 1 && set.Packet()->m_Pos == 1 && set.Packet()->m_DataLen == 1,"each packet set owns exactly one initialized message reference");
        require(set.m_SendFlags == (i ? 0x80u : 0u) && set.m_Rate == (j ? 15 : 5),"packet send policy is native typed state");
        retained[i*2+j].Assign(set.Packet());
    }
    const auto first_packet = first->m_PacketSets[0][0].Packet();
    require(!first->Initialize(84) && first->m_ObjInfos.size() == 42 && first->m_PacketSets[0][0].Packet() == first_packet,"repeat initialization preserves existing ownership");
    auto second = std::make_unique<Client>();
    first->Register(clients); second->Register(clients);
    require(clients.m_nElements == 2 && client_Next(clients,nullptr) == second->Handle() && client_Next(clients,second->Handle()) == first->Handle(),"native handle enumeration follows actual list membership");
    require(!client_Next(clients,first->Handle()) && client_Find(clients,first->Handle()) == first.get(),"native handle lookup and end of enumeration");
    const auto foreign = reinterpret_cast<HCLIENT>(std::uintptr_t(1));
    require(!client_Find(clients,foreign) && !client_Next(clients,foreign),"foreign handles are compared without dereferencing");
    const auto lifetime = first->m_Lifetime;
    const auto removed_handle = first->Handle();
    first.reset();
    require(!*lifetime && clients.m_nElements == 1 && !client_Find(clients,removed_handle),"destruction invalidates callback lifetime and unregisters native client");
    for (auto& message : retained) require(message.m_pMsg->m_nRefCount == 1,"real destructor releases each packet exactly once");
    second->Register(clients);
    require(clients.m_nElements == 1,"repeat registration does not duplicate a list entry");
    second->Unregister(); second->Unregister();
    require(!clients.m_nElements && !client_Next(clients,nullptr),"repeat removal leaves empty registry");
}
void public_state() {
    Client client;
    client.m_Name = "Cate Archer";
    char full[32], short_name[5], one = 'x', untouched = 'x';
    require(client.CopyName(full,sizeof(full)) && std::string(full) == "Cate Archer","owned client name copied");
    require(client.CopyName(short_name,sizeof(short_name)) && std::string(short_name) == "Cate","truncated name always terminated");
    require(client.CopyName(&one,1) && one == 0 && !client.CopyName(nullptr,10) && !client.CopyName(&untouched,0) && !client.CopyName(&untouched,-1) && untouched == 'x',"name buffer boundaries");
    client.m_ClientFlags = CLIENTFLAG_LOCAL | CLIENTFLAG_PLAYBACK | CLIENTFLAG_SKYCHANGED;
    for (uint32 flags = 0; flags < 64; ++flags) {
        client.SetInfoFlags(flags);
        const auto expected = CIF_LOCAL | CIF_PLAYBACK | (flags & (CIF_FULLRES | CIF_SENDCOBJROTATION | CIF_FORCENEXTUPDATE | CIF_AUTOACTIVATEOBJECTS));
        require(client.InfoFlags() == expected && (client.m_ClientFlags & CLIENTFLAG_SKYCHANGED),"public flag mapping preserves internal and read-only flags");
    }
    client.m_CommandStates[0][0] = 1;
    client.m_CommandStates[1][MAX_CLIENTCOMMANDS-1] = 1;
    require(client.CommandOn(0) && !client.CommandOn(MAX_CLIENTCOMMANDS-1),"first command page");
    client.m_nCurCommands = 1;
    require(!client.CommandOn(0) && client.CommandOn(MAX_CLIENTCOMMANDS-1),"second command page");
    require(!client.CommandOn(-1) && !client.CommandOn(std::numeric_limits<int>::min()) && !client.CommandOn(MAX_CLIENTCOMMANDS) && !client.CommandOn(std::numeric_limits<int>::max()),"invalid command IDs never index memory");
    client.m_nCurCommands = 2;
    require(!client.CommandOn(0),"invalid page is bounded");
    LTVector position(1,2,3);
    require(client.SetViewPosition(&position) == LT_OK && client.m_Position.y == 2,"native view position");
    position.x = std::numeric_limits<float>::infinity();
    require(client.SetViewPosition(&position) == LT_INVALIDPARAMS && client.SetViewPosition(nullptr) == LT_INVALIDPARAMS && client.m_Position.x == 1,"invalid view leaves prior state intact");
}
void handshake_data() {
    Client client;
    ServerMessage packet;
    constexpr uint8 bytes[]{4,0,8,16,32,64,128};
    packet.WriteWord(sizeof(bytes)); packet.WriteData(bytes,sizeof(bytes));
    const auto full_length = packet.m_DataLen;
    client.Data() = {99};
    for (uint16 length = 0; length < full_length; ++length) {
        packet.m_DataLen = length; packet.m_Pos = 1;
        require(client.ReceiveData(packet) == LT_INVALIDPARAMS && client.Data() == std::vector<uint8>({99}) &&
                !(client.m_ClientFlags & 0x100) && packet.m_Pos == 1,"every truncated handshake preserves owned data, flags and cursor");
    }
    packet.m_DataLen = full_length; packet.m_Pos = 1;
    require(client.ReceiveData(packet) == LT_OK && packet.m_Pos == full_length &&
            client.Data() == std::vector<uint8>(std::begin(bytes),std::end(bytes)) && (client.m_ClientFlags & 0x100),"complete handshake commits exact bytes including NUL");
    packet.m_pData[3] = 255;
    require(client.Data()[0] == 4,"client data does not borrow packet memory");
    client.m_State = 3;
    packet.m_Pos = 1;
    require(client.ReceiveData(packet) == LT_INVALIDPARAMS && client.Data()[0] == 4,"active-world packets cannot replace retained connection data");
    client.m_State = 0;
    ServerMessage empty; empty.WriteWord(0); empty.m_Pos = 1;
    require(client.ReceiveData(empty) == LT_OK && client.Data().empty(),"empty replacement releases old client data");
    ServerMessage oversized; oversized.WriteWord(UINT16_MAX); oversized.m_Pos = 1;
    require(client.ReceiveData(oversized) == LT_INVALIDPARAMS && client.Data().empty(),"claimed lengths are bounded before allocation");
}
void view_attachments() {
    LTList registry;
    auto first = std::make_unique<Client>();
    auto second = std::make_unique<Client>();
    auto third = std::make_unique<Client>();
    Client other;
    first->Register(registry); second->Register(registry); third->Register(registry);
    require(second->AttachTo(*first) && third->AttachTo(*second),"native parent-child view chain");
    require(first->m_AttachHead.m_pNext->m_pData == second.get() && second->m_AttachHead.m_pNext->m_pData == third.get(),"attachment list payload retains native identity");
    require(!first->AttachTo(*first) && !first->AttachTo(*third) && !first->m_pViewClient,"self and indirect view cycles rejected before mutation");
    require(second->AttachTo(other) && first->m_AttachHead.IsTiedOff() && second->m_pViewClient == &other,"reparent atomically removes old list membership");
    other.m_Removing = true;
    require(!first->AttachTo(other) && !first->m_pViewClient,"cannot attach to a departing client");
    second.reset();
    require(!third->m_pViewClient && third->m_AttachLink.IsTiedOff() && other.m_AttachHead.IsTiedOff() && registry.m_nElements == 2,"destruction detaches parent and all children");
    require(third->AttachTo(*first),"attach after prior parent destruction");
    first.reset();
    require(!third->m_pViewClient && registry.m_nElements == 1,"parent removal restores surviving client ownership");
    require(!third->Detach() && !third->Detach(),"detach is idempotent");
}
void world_readiness_and_tables() {
    LTList registry;
    Client first, second;
    require(first.Initialize(3) && second.Initialize(2),"initialize client object tables");
    first.Register(registry); second.Register(registry);
    first.m_ObjInfos[1].m_ChangeFlags = 17; second.m_ObjInfos[1].m_Unknown02 = 23;
    require(client_GrowObjectInfos(registry,80) && first.m_ObjInfos.size() == 80 && second.m_ObjInfos.size() == 80 &&
            first.m_ObjInfos[1].m_ChangeFlags == 17 && second.m_ObjInfos[1].m_Unknown02 == 23 && !first.m_ObjInfos[79].m_ChangeFlags,"all client tables grow with preserved state and zeroed new records");
    require(!client_GrowObjectInfos(registry,UINT32_MAX) && first.m_ObjInfos.size() == 80,"invalid growth leaves all clients unchanged");
    require(client_GrowObjectInfos(registry,2) && first.m_ObjInfos.size() == 80,"growth never truncates existing object state");
    first.m_FileTransfer = std::make_unique<CClientFiles>(CPacketHolderDesc{},0);
    require(first.m_FileTransfer->Add("required.abc",3,1,3) && first.m_FileTransfer->Add("optional.wav",3,2,2),"readiness uses actual owned transfer entries");
    require(!first.ReadyForWorld(false),"world entry waits for handshake data");
    first.m_ClientFlags |= 0x100;
    require(!first.ReadyForWorld(false),"world entry waits for required resources");
    ServerMessage reply; reply.m_pData[0] = 0x36; reply.WriteWord(0x8001); reply.m_Pos = 1;
    require(first.m_FileTransfer->Handle(reply) && first.ReadyForWorld(false),"optional resources do not block initial world entry");
    first.m_ClientFlags |= 1;
    require(!first.ReadyForWorld(false),"strict readiness waits for all files");
    ServerMessage all; all.m_pData[0] = 0x36; all.WriteWord(0x8002); all.m_Pos = 1;
    require(first.m_FileTransfer->Handle(all) && first.ReadyForWorld(false) && !first.ReadyForWorld(true),"world transition gate remains authoritative");
    first.m_EnteringWorld = true;
    require(!first.ReadyForWorld(false),"entry callback cannot recursively enter the world");
    first.m_EnteringWorld = false; first.m_ExitingWorld = true;
    require(!first.ReadyForWorld(false),"exit callback cannot reenter before detachment completes");
    first.m_ExitingWorld = false;
    first.m_Removing = true;
    require(!first.ReadyForWorld(false),"departing client cannot enter world");
}
void light_changes_and_ids() {
    Client client;
    for (uint32 i = 0; i < MAX_CLIENTLACHANGES; ++i) require(client.QueueLightChange(i,1),"native light queue fills");
    require(client.QueueLightChange(7,4) && client.m_nLightChanges == MAX_CLIENTLACHANGES && client.m_LightChanges[7].m_ChangeFlags == 5,"repeated animation merges flags even when queue is full");
    require(!client.QueueLightChange(1000,1) && !client.QueueLightChange(UINT32_MAX,1) && !client.QueueLightChange(7,UINT32_MAX),"queue limits and wire-width validation preserve prior state");
    uint32 index = 999;
    require(la_FindClientChange(&client,7,&index) && index == 7 && !la_FindClientChange(&client,65536+7,&index) &&
            !la_FindClientChange(nullptr,7,&index) && !la_FindClientChange(&client,7,nullptr),"light lookup cannot alias truncated animation IDs");
    ClientIDList ids;
    for (uint32 i = 0; i < UINT16_MAX; ++i) require(ids.Append(static_cast<uint16>(i)),"native ID list grows without uint16 capacity wrap");
    require(!ids.Append(0) && ids.m_IDs.size() == UINT16_MAX && ids.m_IDs.back() == UINT16_MAX-1,"wire count bounds ID list growth");
    ids.Clear(); require(ids.m_IDs.empty() && ids.Append(3) && ids.m_IDs[0] == 3,"ID list reuses owned storage");
}
void command_callback_lifetime() {
    struct Receiver {
        unsigned calls = 0;
        std::function<void()> action;
        static void Notify(void* context, HCLIENT, uint8 command, bool active) {
            auto& receiver = *static_cast<Receiver*>(context);
            require(command == 3 || command == 4,"actual command ID forwarded");
            require(active == (command == 3),"actual command state forwarded");
            ++receiver.calls;
            auto once = std::move(receiver.action);
            if (once) once();
        }
    };
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        auto client = std::make_unique<Client>();
        client->m_State = 3;
        client->m_CommandStates[0][3] = 1;
        // The command list itself may reside inside the object destroyed
        // by the first game callback; no subsequent byte may be read.
        auto& input = client->m_CommandStates[1]; input[0] = 3; input[1] = 4;
        Receiver receiver;
        switch (scenario) {
        case 0: break;
        case 1: receiver.action = [&] { client.reset(); }; break;
        case 2: receiver.action = [&] { client->m_Removing = true; }; break;
        case 3: receiver.action = [&] { client->m_State = 1; }; break;
        case 4: receiver.action = [&] { client->m_nCurCommands = 1; }; break;
        }
        client_DispatchCommands(*client,std::span<const uint8>(input,2),Receiver::Notify,&receiver);
        require(receiver.calls == (scenario ? 1u : 2u),"game callbacks cancel stale command dispatch after client destruction or state changes");
    }
}
void visit_and_data_lifetime() {
    LTList clients;
    auto first = std::make_unique<Client>(), second = std::make_unique<Client>();
    first->Register(clients); second->Register(clients);
    first->Data() = {1,2,3,4};
    const auto data = first->RetainData();
    const auto* original = data->data();
    struct Context { std::unique_ptr<Client>& first; std::unique_ptr<Client>& second; unsigned calls = 0; } context{first,second};
    require(client_Visit(clients,[](void* pointer,Client&) {
        auto& context = *static_cast<Context*>(pointer);
        ++context.calls;
        context.first.reset(); context.second.reset();
    },&context),"visit native registered clients");
    require(context.calls == 1 && !clients.m_nElements,"client visit tolerates destruction of current and later records");
    require(data->data() == original && *data == std::vector<uint8>({1,2,3,4}),"connection data remains valid throughout a callback that removes the client");
}
}
int main() {
    try {
        packets_and_registry(); public_state(); handshake_data(); view_attachments(); world_readiness_and_tables(); light_changes_and_ids(); command_callback_lifetime(); visit_and_data_lifetime();
        std::cout << "native client ownership and SDK state tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
