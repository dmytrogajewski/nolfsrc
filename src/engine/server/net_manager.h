#pragma once
#include "net_driver.h"
#include "net_packet.h"
#include "collection.h"
class CBaseMgr
{
public:
	virtual ~CBaseMgr() {}
	virtual LTBOOL _v04(class NetConnection*, uint32 flags) = 0;
	virtual void     _v08(class NetConnection* pConn) = 0;
	virtual void     ProcessPacket(class CPacket* pPacket, uint8* senderAddr,
	                               uint16 senderPort) = 0;
	virtual void _v10(int nEvent, char* pName, int n);
};
using CConnList = CMoArray<NetConnection*>;
using CDriverList = CMoArray<CBaseDriver*>;
class CNetQueueA : public LMessageList
{
public:
	NOLF_FORCE_INLINE CNetQueueA() { m_nMessages = 0; m_pTail = LTNULL; }
	~CNetQueueA() { m_pTail = LTNULL; m_nMessages = 0; }
};
class CNetQueueB : public CNetSentQueue
{
public:
	NOLF_FORCE_INLINE CNetQueueB() { m_nMessages = 0; m_pTail = LTNULL; }
	virtual LTBOOL GenAppend(LThreadMessage*& toAppend)
	{
		Prepend(toAppend);
		return LTTRUE;
	}
	~CNetQueueB() { m_pTail = LTNULL; m_nMessages = 0; }
};
class CNetMgr : public NetDriverHost
{
public:
    LTGUID application_guid() const override { return m_Guid; }
    bool accept_connection(NetConnection&) override;
    void close_connection(NetConnection&) override;
    void release_buffers(NetConnection&) noexcept override;
    void flush_connection(NetConnection&) override;
    void connectionless_packet(ServerMessage&, const nolf::platform::Endpoint&) override;
    void remote_disconnect(uint32 reason) override;
    CNetMgr();
	~CNetMgr();
	LTRESULT Init();
	void     Term();
	LTBOOL   SetNetName(char* pName);
	LTRESULT GetServiceList(NetService*& pListHead);
	LTRESULT FreeServiceList(NetService* pListHead);
	LTRESULT GetSessionList(NetSession*& pListHead, char* pInfo);
	LTRESULT FreeSessionList(NetSession* pListHead);
	LTRESULT SelectService(HNETSERVICE hNetService);
	LTRESULT GetSessionName(char* sName, uint32 dwBufferSize);
	LTRESULT UpdateSessionName(const char* sName);
	LTRESULT GetTcpIpAddress(char* sAddress, uint32 dwBufferSize,
	                         unsigned short& hostPort);
	void     Disconnect(NetConnection* pConn, uint32 nReason, uint32 flags);
	void     Update(char* pTraceName, float fCurTime, int bAllowTimeout);
	void     StartGettingPackets();
	void     EndGettingPackets();
	LTBOOL   GetPacket(class ServerMessage* pMsg);
	void  TermDrivers();
	void  RemoveDriver(class CBaseDriver* pDriver);
	class CBaseDriver* AddDriver(char* pDriverName);
	class CBaseDriver* FindDriver(char* pDriverName);
	void  TermQueue(LMessageList* pQueue);
	void  TermConnections();
	void  RemoveConnection(NetConnection* pConn);
	void  SetGuid(LTGUID* pGuid);
	void  AddRecvBytes(NetConnection* pConn, uint32 nBytes);
	void  ReleaseQueued(CNetPending* pRec);
	LTBOOL AddConnection(NetConnection* pConn);
	void  CloseConnection(NetConnection* pConn);
	int   TranslatePacket(class ServerMessage* pMsg, NetConnection* pConn,
	                      int bRaw);
	void  FreeRecvPacket(CNetRecvPacket* pRec);
	void* FindQueuedPacket(LMessageList* pList, uint32 nID);
	LTBOOL SendPacket(class ServerMessage* pMsg, NetConnection* pConn, uint8 n);
	LTBOOL SendPacketData(ServerMessage* pData, NetConnection* pConn);
	void  WriteAckPacket(NetConnection* pConn, class ServerMessage* pMsg);
	void  DebugPrint(NetConnection* pConn, int nLevel, char* pFormat, ...);
    void   AddSentBytes(NetConnection* pConn, uint32 nBytes);
    void   DebugPrint(int nLevel, char* pFormat, ...);
    int    sub_10023a8c(NetConnection* pConn, class ServerMessage* pMsg);
    int    sub_100235e8(NetConnection* pConn, class ServerMessage* pMsg);
    LTBOOL SendToConnection(class ServerMessage* pMsg, NetConnection* pConn,
	                        int nFlags);
    void   sub_1002294f(NetConnection* pConn);
    void   sub_10023ad7(LMessageList* pList, uint32 nID);
    class CNetFragRec* sub_100233ad(uint32 nID, NetConnection* pConn);
    class CNetFragRec* sub_10023495();
    LTBOOL sub_100233e3(class ServerMessage* pMsg, uint8 nIndex,
	                    class CNetFragRec* pRec);
    int    sub_10023474(class CNetFragRec* pRec);
    void   sub_10022ac2(NetConnection* pConn);
    void   sub_10023a91(LMessageList* pList, uint32 nID);
    CNetRecvPacket* sub_10023c4d(int nLen);
    LTBOOL sub_10023b1f(LMessageList* pList, CNetRecvPacket* pRec);
    void   sub_10023d16(CMessageRef* pRef);
    void   sub_10023d5b(class ServerMessage* pDst, class ServerMessage* pSrc);
    void   sub_10023d73(class ServerMessage* pMsg, void* pData, uint32 nLen);
    LTBOOL sub_10022c2f(void* pData, uint32 nLen, uint32 nSpare, NetConnection* pConn);
public:
	uint32        m_Flags;
	CNetBank      m_PacketBank;
	CConnList     m_Connections;
	CDriverList   m_Drivers;
	char          m_Name[100];
	class CBaseMgr* m_pHandler;
	CNetQueueA    m_QueueB8;
	CNetQueueB    m_QueueC4;
	char*         m_pTraceName;
	float         m_FrameTime;
	uint32        m_UnknownD8;
	uint32        m_UnknownDC;
	CRateTracker  m_RateE0;
	float         m_LastTime;
	LTGUID        m_Guid;
	class CBaseDriver* m_pMainDriver;
};
