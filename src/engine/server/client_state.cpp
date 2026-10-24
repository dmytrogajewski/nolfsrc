#include "client_state.h"
#include "iltserver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

Client::Client() {
    m_Link.Init2(this);
    m_AttachLink.Init2(this);
    m_AttachHead.Init2(this);
}
Client::~Client() {
    *m_Lifetime = false;
    Detach();
    while (m_AttachHead.m_pNext != &m_AttachHead)
        static_cast<Client*>(m_AttachHead.m_pNext->m_pData)->Detach();
    Unregister();
}
bool Client::Initialize(uint32 capacity) {
    if (initialized_ || capacity > 0xfff + 40) return false;
    try {
        std::vector<CObjInfo> infos(capacity);
        CMessageRef packets[2][2];
        for (auto& row : packets) for (auto& packet : row) {
            CAbstractIO* owner = nullptr;
            packet.Init(msg_AllocPacket(&owner,0x1000,0x1000));
            if (owner) owner->Release();
            if (!packet.m_pMsg) return false;
        }
        m_ObjInfos.swap(infos);
        for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 2; ++j) {
            m_PacketSets[i][j].m_Message.Assign(packets[i][j].m_pMsg);
            m_PacketSets[i][j].m_SendFlags = i ? 0x80 : 0;
            m_PacketSets[i][j].m_Rate = j ? 15 : 5;
        }
        m_RateControl.SetRate(10);
        m_ClientFlags |= CLIENTFLAG_SENDCOBJROTATION;
        m_State = 1;
        initialized_ = true;
        return true;
    } catch (const std::bad_alloc&) { return false; }
}
CSoundSentState* snd_FindSentState(Client* client, uint16 fileID) {
    if (!client) return nullptr;
    try { return &client->m_SoundStates.try_emplace(fileID).first->second; }
    catch (const std::bad_alloc&) { return nullptr; }
}
bool client_QueueSound(LTList& clients, std::shared_ptr<const SoundRecord> record) {
    if (!record) return false;
    auto eligible = [](Client* client) { return client->m_State == 3 && !client->m_Removing; };
    try {
        for (auto* node = clients.m_Head.m_pNext; node != &clients.m_Head; node = node->m_pNext) {
            auto* client = static_cast<Client*>(node->m_pData);
            if (eligible(client)) {
                if (client->m_QueuedSounds.size() == client->m_QueuedSounds.max_size()) return false;
                client->m_QueuedSounds.reserve(client->m_QueuedSounds.size() + 1);
            }
        }
    } catch (const std::bad_alloc&) { return false; }
    for (auto* node = clients.m_Head.m_pNext; node != &clients.m_Head; node = node->m_pNext) {
        auto* client = static_cast<Client*>(node->m_pData);
        if (eligible(client)) client->m_QueuedSounds.push_back(record);
    }
    return true;
}
void Client::Register(LTList& list) {
    Unregister();
    dl_AddHead(&list,&m_Link,this);
    registry_ = &list;
}
void Client::Unregister() {
    if (!registry_) return;
    dl_RemoveAt(registry_,&m_Link);
    registry_ = nullptr;
}
Client* client_Find(const LTList& list, HCLIENT handle) {
    if (!handle) return nullptr;
    for (auto* link = list.m_Head.m_pNext; link != &list.m_Head; link = link->m_pNext)
        if (reinterpret_cast<HCLIENT>(link->m_pData) == handle) return static_cast<Client*>(link->m_pData);
    return nullptr;
}
HCLIENT client_Next(const LTList& list, HCLIENT previous) {
    auto* link = list.m_Head.m_pNext;
    if (previous) {
        auto* client = client_Find(list,previous);
        if (!client) return nullptr;
        link = client->m_Link.m_pNext;
    }
    return link == &list.m_Head ? nullptr : reinterpret_cast<HCLIENT>(link->m_pData);
}
bool Client::CopyName(char* output, int capacity) const {
    if (!output || capacity <= 0) return false;
    const auto count = std::min(m_Name.size(),static_cast<std::size_t>(capacity - 1));
    std::memmove(output,m_Name.data(),count);
    output[count] = 0;
    return true;
}
uint32 Client::InfoFlags() const {
    uint32 flags = 0;
    if (m_ClientFlags & CLIENTFLAG_LOCAL) flags |= CIF_LOCAL;
    if (m_ClientFlags & CLIENTFLAG_PLAYBACK) flags |= CIF_PLAYBACK;
    if (m_ClientFlags & CLIENTFLAG_FULLRES) flags |= CIF_FULLRES;
    if (m_ClientFlags & CLIENTFLAG_SENDCOBJROTATION) flags |= CIF_SENDCOBJROTATION;
    if (m_ClientFlags & CLIENTFLAG_FORCENEXTUPDATE) flags |= CIF_FORCENEXTUPDATE;
    if (m_ClientFlags & CLIENTFLAG_AUTOACTIVATE) flags |= CIF_AUTOACTIVATEOBJECTS;
    return flags;
}
void Client::SetInfoFlags(uint32 flags) {
    m_ClientFlags &= ~(CLIENTFLAG_FULLRES | CLIENTFLAG_SENDCOBJROTATION | CLIENTFLAG_FORCENEXTUPDATE | CLIENTFLAG_AUTOACTIVATE);
    if (flags & CIF_FULLRES) m_ClientFlags |= CLIENTFLAG_FULLRES;
    if (flags & CIF_SENDCOBJROTATION) m_ClientFlags |= CLIENTFLAG_SENDCOBJROTATION;
    if (flags & CIF_FORCENEXTUPDATE) m_ClientFlags |= CLIENTFLAG_FORCENEXTUPDATE;
    if (flags & CIF_AUTOACTIVATEOBJECTS) m_ClientFlags |= CLIENTFLAG_AUTOACTIVATE;
}
bool Client::CommandOn(int command) const {
    return command >= 0 && command < MAX_CLIENTCOMMANDS && m_nCurCommands < 2 &&
           m_CommandStates[m_nCurCommands][command] != 0;
}
LTRESULT Client::SetViewPosition(const LTVector* position) {
    if (!position || !std::isfinite(position->x) || !std::isfinite(position->y) || !std::isfinite(position->z)) return LT_INVALIDPARAMS;
    m_Position = *position;
    return LT_OK;
}
LTRESULT Client::ReceiveData(ServerMessage& packet) {
    if (m_Removing || m_State == 3) return LT_INVALIDPARAMS;
    if (!packet.CanRead(2)) return LT_INVALIDPARAMS;
    const auto original = packet.m_Pos;
    const auto count = packet.ReadWord(nullptr);
    if (!packet.CanRead(count)) { packet.m_Pos = original; return LT_INVALIDPARAMS; }
    try {
        auto data = std::make_shared<std::vector<uint8>>(packet.m_pData + packet.m_Pos, packet.m_pData + packet.m_Pos + count);
        data_.swap(data);
        packet.m_Pos += count;
        m_ClientFlags |= 0x100;
        return LT_OK;
    } catch (const std::bad_alloc&) { packet.m_Pos = original; return LT_OUTOFMEMORY; }
}
bool Client::ReadyForWorld(bool between_worlds) const {
    if (between_worlds || m_Removing || m_EnteringWorld || m_ExitingWorld || !(m_ClientFlags & 0x100)) return false;
    if (m_ClientFlags & CLIENTFLAG_LOCAL) return true;
    if (!m_FileTransfer) return false;
    return (m_ClientFlags & 1) ? m_FileTransfer->file_count() == 0 : m_FileTransfer->counted_count() == 0;
}
bool Client::QueueLightChange(uint32 animation, uint32 flags) {
    if (animation > UINT16_MAX || flags > UINT16_MAX) return false;
    uint32 index = 0;
    if (la_FindClientChange(this,animation,&index)) {
        m_LightChanges[index].m_ChangeFlags |= static_cast<uint16>(flags);
        return true;
    }
    if (m_nLightChanges >= m_LightChanges.size()) return false;
    m_LightChanges[m_nLightChanges++] = {static_cast<uint16>(animation),static_cast<uint16>(flags)};
    return true;
}
bool Client::AttachTo(Client& parent) {
    for (auto* ancestor = &parent; ancestor; ancestor = ancestor->m_pViewClient)
        if (ancestor == this || ancestor->m_Removing) return false;
    if (m_Removing) return false;
    Detach();
    m_pViewClient = &parent;
    parent.m_AttachHead.AddAfter(&m_AttachLink);
    return true;
}
bool Client::Detach() {
    const bool attached = m_pViewClient != nullptr;
    m_AttachLink.Remove();
    m_pViewClient = nullptr;
    return attached;
}
bool ClientIDList::Append(uint16 id) {
    if (m_IDs.size() >= std::numeric_limits<uint16>::max()) return false;
    try { m_IDs.push_back(id); return true; }
    catch (const std::bad_alloc&) { return false; }
}
int la_FindClientChange(Client* client, uint32 animation, uint32* index) {
    if (!client || !index || animation > UINT16_MAX) return 0;
    const auto count = std::min<uint32>(client->m_nLightChanges,client->m_LightChanges.size());
    for (uint32 i = 0; i < count; ++i) if (client->m_LightChanges[i].m_hLightAnim == animation) {
        *index = i;
        return 1;
    }
    return 0;
}
bool client_GrowObjectInfos(const LTList& clients, uint32 capacity) {
    if (capacity > 0xfff + 40) return false;
    struct Pending { Client* client; std::vector<CObjInfo> data; };
    std::vector<Pending> pending;
    try {
        for (auto* link = clients.m_Head.m_pNext; link != &clients.m_Head; link = link->m_pNext) {
            auto* client = static_cast<Client*>(link->m_pData);
            if (capacity <= client->m_ObjInfos.size()) continue;
            Pending next{client,client->m_ObjInfos};
            next.data.resize(capacity);
            pending.push_back(std::move(next));
        }
    } catch (const std::bad_alloc&) { return false; }
    for (auto& next : pending) next.client->m_ObjInfos.swap(next.data);
    return true;
}
void client_DispatchCommands(Client& client, std::span<const uint8> commands, ClientCommandNotify notify, void* context) {
    if (!notify) return;
    const auto lifetime = client.m_Lifetime;
    const auto state = client.m_State;
    const auto page = client.m_nCurCommands;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (!*lifetime || client.m_Removing || client.m_State != state || client.m_nCurCommands != page) return;
        const auto command = commands[i];
        if (command >= MAX_CLIENTCOMMANDS) continue;
        notify(context,client.Handle(),command,client.CommandOn(command));
    }
}
bool client_Visit(const LTList& clients, ClientVisitor visitor, void* context) {
    if (!visitor) return false;
    struct Entry { Client* client; std::shared_ptr<bool> lifetime; };
    std::vector<Entry> entries;
    try {
        entries.reserve(clients.m_nElements);
        for (auto* link = clients.m_Head.m_pNext; link != &clients.m_Head; link = link->m_pNext) {
            auto* client = static_cast<Client*>(link->m_pData);
            entries.push_back({client,client->m_Lifetime});
        }
    } catch (const std::bad_alloc&) { return false; }
    for (const auto& entry : entries)
        if (*entry.lifetime && !entry.client->m_Removing) visitor(context,*entry.client);
    return true;
}
