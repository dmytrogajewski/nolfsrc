#pragma once
#include "sound_sent_state.h"
#include "sound_record.h"
#include <map>
#include "counter.h"
#include "file_transfer.h"
#include "message.h"
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <vector>

#define CLIENTFLAG_LOCAL            0x0002
#define CLIENTFLAG_FULLRES          0x0004
#define CLIENTFLAG_SENDCOBJROTATION 0x0008
#define CLIENTFLAG_FORCENEXTUPDATE  0x0010
#define CLIENTFLAG_PLAYBACK         0x0040
#define CLIENTFLAG_AUTOACTIVATE     0x0200
#define CLIENTFLAG_SKYCHANGED       0x0400
#define MAX_CLIENTCOMMANDS 0xff
#define MAX_CLIENTLACHANGES 0x40

struct ClientPacketSet {
    CMessageRef m_Message;
    float m_Rate = 0;
    uint32 m_SendFlags = 0;
    ServerMessage* Packet() const { return m_Message.m_pMsg; }
};
struct CObjInfo {
    uint16 m_ChangeFlags = 0;
    uint8 m_Unknown02 = 0, m_Unknown03 = 0;
};
struct ClientIDList {
    bool Append(uint16 id);
    void Clear() { m_IDs.clear(); }
    std::vector<uint16> m_IDs;
};
struct ClientLightChange {
    uint16 m_hLightAnim = 0, m_ChangeFlags = 0;
};

// A connection's engine state has one native representation. It owns its
// packets, transfer streams, strings, user data bytes and per-object tables.
// The server drains its message/sound record pools before deleting a client.
class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool Initialize(uint32 object_capacity);
    void Register(LTList&);
    void Unregister();
    HCLIENT Handle() { return reinterpret_cast<HCLIENT>(this); }
    bool CopyName(char* output, int capacity) const;
    uint32 InfoFlags() const;
    void SetInfoFlags(uint32 flags);
    bool CommandOn(int command) const;
    LTRESULT SetViewPosition(const LTVector*);
    LTRESULT ReceiveData(ServerMessage&);
    std::vector<uint8>& Data() { return *data_; }
    const std::vector<uint8>& Data() const { return *data_; }
    std::shared_ptr<std::vector<uint8>> RetainData() const { return data_; }
    bool ReadyForWorld(bool between_worlds) const;
    bool QueueLightChange(uint32 animation, uint32 flags);
    bool AttachTo(Client& parent);
    bool Detach();

    LTLink m_Link, m_AttachLink, m_AttachHead;
    std::array<ClientLightChange, MAX_CLIENTLACHANGES> m_LightChanges{};
    uint32 m_nLightChanges = 0;
    CRateController m_RateControl;
    float m_Unknown128 = 10, m_ViewRate = 35;
    ClientPacketSet m_PacketSets[2][2];
    Client* m_pViewClient = nullptr;
    LTVector m_Position{0,0,0};
    std::unique_ptr<CClientFiles> m_FileTransfer;
    std::vector<CObjInfo> m_ObjInfos;
    ClientIDList m_IDLists[2];
    int m_CurIDList = 0;
    uint8 m_CommandStates[2][MAX_CLIENTCOMMANDS]{};
    uint32 m_nCurCommands = 0;
    LTObject* m_pUnknown3AC = nullptr;
    void* m_pUserData = nullptr;
    uint16 m_ClientID = 0xffff;
    int m_State = 0, m_Unknown3BC = 0;
    uint32 m_ClientFlags = 0;
    std::string m_Name;
    NetConnection* m_pConnection = nullptr;
    std::vector<std::shared_ptr<const SoundRecord>> m_QueuedSounds;
    std::map<uint16, CSoundSentState> m_SoundStates;
    bool m_Removing = false;
    bool m_EnteringWorld = false, m_ExitingWorld = false;
    std::shared_ptr<bool> m_Lifetime = std::make_shared<bool>(true);
private:
    LTList* registry_ = nullptr;
    bool initialized_ = false;
    std::shared_ptr<std::vector<uint8>> data_ = std::make_shared<std::vector<uint8>>();
};
// Reserve every eligible client's queue before publishing any record.
bool client_QueueSound(LTList&, std::shared_ptr<const SoundRecord>);

// Lookup compares opaque handles before dereferencing client storage. A
// foreign or removed handle cannot walk an arbitrary address as a list node.
Client* client_Find(const LTList&, HCLIENT);
HCLIENT client_Next(const LTList&, HCLIENT previous);
int la_FindClientChange(Client*, uint32 animation, uint32* index);
bool client_GrowObjectInfos(const LTList&, uint32 capacity);
using ClientCommandNotify = void (*)(void*, HCLIENT, uint8 command, bool active);
void client_DispatchCommands(Client&, std::span<const uint8>, ClientCommandNotify, void*);
using ClientVisitor = void (*)(void*, Client&);
bool client_Visit(const LTList&, ClientVisitor, void*);
