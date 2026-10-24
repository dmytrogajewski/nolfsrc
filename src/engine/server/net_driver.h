#pragma once
#include "ltbasedefs.h"
#include "net_connection.h"
#include "platform/sockets.h"

class ServerMessage;
class CNetMgr;
class CBaseDriver;

// Engine policy lives in the manager. Transports own sockets, peers and
// discovery state, and report changes through this typed boundary.
class NetDriverHost {
public:
    virtual ~NetDriverHost() = default;
    virtual LTGUID application_guid() const = 0;
    virtual bool accept_connection(NetConnection&) = 0;
    virtual void close_connection(NetConnection&) = 0;
    virtual void release_buffers(NetConnection&) noexcept = 0;
    virtual void flush_connection(NetConnection&) = 0;
    virtual void connectionless_packet(ServerMessage&, const nolf::platform::Endpoint&) = 0;
    virtual void remote_disconnect(uint32 reason) = 0;
};

inline constexpr uint32 NETDRIVER_TCPIP = 1;
using CNetService = HNETSERVICE_t;

class CBaseDriver {
public:
    virtual ~CBaseDriver() = default;
    virtual LTBOOL Init() = 0;
    virtual void Term() = 0;
    virtual void Update() = 0;
    virtual void Connect(CBaseDriver*) {}
    virtual LTRESULT GetServiceList(NetService**) { return LT_ERROR; }
    virtual int SelectService(HNETSERVICE) { return 1; }
    virtual LTRESULT GetSessionList(NetSession**, const char*) { return LT_ERROR; }
    virtual LTRESULT StartQuery(const char*) { return LT_ERROR; }
    virtual LTRESULT UpdateQuery() { return LT_ERROR; }
    virtual LTRESULT GetQueryResults(NetSession**) { return LT_ERROR; }
    virtual LTRESULT EndQuery() { return LT_ERROR; }
    virtual LTRESULT HostSession(NetHost*) { return LT_ERROR; }
    virtual LTRESULT JoinSession(NetSession*) { return LT_ERROR; }
    virtual LTRESULT UpdateSessionName(const char*) { return LT_ERROR; }
    virtual LTRESULT GetSessionName(char*, uint32) { return LT_ERROR; }
    virtual LTBOOL IsLobbyLaunched() { return LTFALSE; }
    virtual LTBOOL GetLobbyLaunchInfo(void**) { return LTFALSE; }
    virtual LTRESULT GetTcpIpAddress(char*, uint32, unsigned short&) { return LT_ERROR; }
    virtual LTRESULT ConnectTCP(const char*) { return LT_ERROR; }
    virtual LTRESULT SendTo(const void*, uint32, const char*, uint32) { return LT_ERROR; }
    virtual void Disconnect(NetConnection*, uint32 reason, uint32 flags) = 0;
    virtual LTBOOL SendData(void*, uint32 length, uint32 spare, NetConnection*) = 0;
    virtual LTBOOL ReceivePacket(ServerMessage*) = 0;
    virtual uint32 GetRecvSize() = 0;

    void set_host(NetDriverHost* value) { host_ = value; }
    NetDriverHost* host() const { return host_; }
    char m_Name[64]{};
    uint32 m_DriverFlags = 0;
    CNetMgr* m_pNetMgr = nullptr;
    float m_Timeout = 120.0f;
private:
    NetDriverHost* host_ = nullptr;
};

CBaseDriver* tcp_Create();
CBaseDriver* ldrv_Create();
