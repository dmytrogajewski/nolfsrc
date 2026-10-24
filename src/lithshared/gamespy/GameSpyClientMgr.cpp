// Native GameSpy browser: asynchronous TCP master exchange and bounded UDP status queries.
#include "GameSpyClientMgr.h"
#include "gs_protocol.h"
#include "platform/sockets.h"
#include "platform/system.h"
#include "platform/logging.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

static std::uint64_t milliseconds() { return nolf::platform::monotonic_nanoseconds()/1000000; }
static std::string address_text(std::uint32_t address) {
    unsigned char bytes[4]; std::memcpy(bytes,&address,4);
    return std::to_string(bytes[0])+'.'+std::to_string(bytes[1])+'.'+std::to_string(bytes[2])+'.'+std::to_string(bytes[3]);
}

#define GSL_MAX_SERVERS		1024	// servers taken from one master list
#define GSL_MAX_PAIRS		224		// keys kept per server
#define GSL_DATA_SIZE		6144	// key and value text kept per server
#define GSL_MAX_KEY			63
#define GSL_MAX_VALUE		100		// the Join folder formats values into 128-byte buffers
#define GSL_LIST_BUFFER		(GSL_MAX_SERVERS * 6 + 1024)
#define GSL_LIST_TIMEOUT	10000	// ms for the whole master conversation
#define GSL_QUERY_RETRY		1000	// ms before a query is sent again
#define GSL_QUERY_TRIES		3		// sends before a server is given up (SERVER_TIMEOUT)

enum { GSS_WAITING, GSS_QUERYING, GSS_DONE, GSS_FAILED };

struct GServerImplementation
{
	std::uint32_t	nIP;			// network order
	unsigned short	nPort;			// host order
	int				nState;			// GSS_*
	int				nTries;
	std::uint64_t			dwSent;			// first send, for the ping
	std::uint64_t			dwLastSend;
	int				nPing;
	std::uint64_t packet_mask;
	std::uint32_t query_id;
	int				nPacketsGot;
	int				nPackets;		// from the packet carrying \final\, 0 until then
	int				nPairs;
	int				nDataUsed;
	short			aKey[GSL_MAX_PAIRS];
	short			aValue[GSL_MAX_PAIRS];
	char			sData[GSL_DATA_SIZE];
};

struct GServerListImplementation
{
	int				nState;			// sl_*
	std::unique_ptr<nolf::platform::TcpSocket> hTCP;
	std::unique_ptr<nolf::platform::UdpSocket> hUDP;
	std::uint64_t			dwListStart;
	LTBOOL			bSentRequest;
	std::string request;
	std::size_t request_sent{};
	int				nRecv;
	char			aRecv[GSL_LIST_BUFFER];

	GServerImplementation*	aServers;
	int				nServers;
	int				nNextQuery;
	int				nActive;
	int				nFinished;

	GServerImplementation*	apListed[GSL_MAX_SERVERS];
	int				nListed;
};

static char s_sMasterServer[128] = "";

void GameSpyClient_SetMasterServer(const char* sAddr)
{
	strncpy(s_sMasterServer, sAddr ? sAddr : "", sizeof(s_sMasterServer) - 1);
	s_sMasterServer[sizeof(s_sMasterServer) - 1] = '\0';
}


// --- one server's keys -------------------------------------------------- //

static char* GSL_GetValue(GServerImplementation* pServer, const char* sKey)
{
	if (!pServer || !sKey) return(NULL);
	for (int i = 0; i < pServer->nPairs; i++)
	{
		if (nolf::ascii_compare(&pServer->sData[pServer->aKey[i]], sKey) == 0)
			return(&pServer->sData[pServer->aValue[i]]);
	}
	return(NULL);
}

static void GSL_AddPair(GServerImplementation* pServer, const char* pKey, int nKeyLen,
                        const char* pValue, int nValueLen)
{
	if (nKeyLen <= 0) return;
	if (nKeyLen > GSL_MAX_KEY) nKeyLen = GSL_MAX_KEY;
	if (nValueLen > GSL_MAX_VALUE) nValueLen = GSL_MAX_VALUE;
	if (pServer->nPairs >= GSL_MAX_PAIRS) return;
	if (pServer->nDataUsed + nKeyLen + nValueLen + 2 > GSL_DATA_SIZE) return;

	// the same key again (a repeated packet): keep the first
	char sKey[GSL_MAX_KEY + 1];
	memcpy(sKey, pKey, nKeyLen);
	sKey[nKeyLen] = '\0';
	if (GSL_GetValue(pServer, sKey)) return;

	char* p = &pServer->sData[pServer->nDataUsed];
	pServer->aKey[pServer->nPairs] = (short)pServer->nDataUsed;
	memcpy(p, sKey, nKeyLen + 1);
	p += nKeyLen + 1;
	pServer->aValue[pServer->nPairs] = (short)(p - pServer->sData);
	memcpy(p, pValue, nValueLen);
	p[nValueLen] = '\0';
	pServer->nDataUsed += nKeyLen + nValueLen + 2;
	pServer->nPairs++;
}

static void GSL_AddString(GServerImplementation* pServer, const char* sKey, const char* sValue)
{
	GSL_AddPair(pServer, sKey, (int)strlen(sKey), sValue, (int)strlen(sValue));
}

// One reply packet: \key\value...[\final\]\queryid\<id>.<packet>
static void GSL_ParseReply(GServerImplementation* server,const char* data,int size) {
    const std::string_view packet(data,static_cast<std::size_t>(size));
    bool final=false;
    unsigned number=1,query_id=0;
    std::vector<std::pair<std::string_view,std::string_view>> pairs;
    std::size_t cursor=0;
    while (cursor<packet.size() && packet[cursor]==char(92)) {
        const auto start=++cursor;
        const auto delimiter=packet.find(char(92),start);
        if (delimiter==packet.npos) return;
        const auto key=packet.substr(start,delimiter-start);
        cursor=delimiter+1;
        if (nolf::ascii_compare(key,"final")==0) { final=true; if (cursor==packet.size()) break; if (packet[cursor]!=char(92)) return; continue; }
        const auto end=packet.find(char(92),cursor);
        const auto value=packet.substr(cursor,end==packet.npos ? packet.size()-cursor : end-cursor);
        cursor=end==packet.npos ? packet.size() : end;
        if (nolf::ascii_compare(key,"queryid")==0) {
            const auto dot=value.find('.');
            if (dot==value.npos) return;
            const auto id=std::from_chars(value.data(),value.data()+dot,query_id);
            const auto part=std::from_chars(value.data()+dot+1,value.data()+value.size(),number);
            if (id.ec!=std::errc{} || id.ptr!=value.data()+dot || part.ec!=std::errc{} || part.ptr!=value.data()+value.size()) return;
        } else pairs.emplace_back(key,value);
    }
    if (!number || number>64 || (server->packet_mask && server->query_id!=query_id)) return;
    const auto bit=std::uint64_t{1}<<(number-1);
    if (server->packet_mask & bit) return;
    if (final) {
        if (server->nPackets && server->nPackets!=static_cast<int>(number)) return;
        server->nPackets=static_cast<int>(number);
    }
    server->query_id=query_id; server->packet_mask |= bit; ++server->nPacketsGot;
    for (const auto& [key,value] : pairs) GSL_AddPair(server,key.data(),static_cast<int>(key.size()),value.data(),static_cast<int>(value.size()));
}
static bool GSL_ReplyComplete(const GServerImplementation* server) {
    if (!server->nPackets) return false;
    const auto expected=server->nPackets==64 ? ~std::uint64_t{0} : (std::uint64_t{1}<<server->nPackets)-1;
    return (server->packet_mask & expected)==expected;
}

// --- the list ------------------------------------------------------------ //

static void GSL_CloseSockets(GServerListImplementation* list) { list->hTCP.reset(); list->hUDP.reset(); }

static void GSL_Reset(GServerListImplementation* pList)
{
	GSL_CloseSockets(pList);
	if (pList->aServers) free(pList->aServers);
	pList->aServers     = NULL;
	pList->nServers     = 0;
	pList->nNextQuery   = 0;
	pList->nActive      = 0;
	pList->nFinished    = 0;
	pList->nListed      = 0;
	pList->nRecv        = 0;
	pList->bSentRequest = LTFALSE;
    pList->request.clear(); pList->request_sent=0;
	pList->nState       = sl_idle;
}

// The master's binary list (6 bytes per server) -> aServers.
static void GSL_TakeList(GServerListImplementation* pList, const char* pData, int nLen)
{
	int nCount = nLen / 6;
	if (nCount > GSL_MAX_SERVERS) nCount = GSL_MAX_SERVERS;
	if (nCount <= 0) return;

	pList->aServers = (GServerImplementation*)malloc(nCount * sizeof(GServerImplementation));
	if (!pList->aServers) return;

	for (int i = 0; i < nCount; i++)
	{
		const unsigned char* p = (const unsigned char*)pData + i * 6;
		std::uint32_t nIP;
		unsigned short nPort = (unsigned short)((p[4] << 8) | p[5]);
		memcpy(&nIP, p, 4);
		if (!nIP || nIP == 0xffffffffu || !nPort) continue;

		LTBOOL bDuplicate = LTFALSE;
		for (int j = 0; j < pList->nServers; j++)
		{
			if (pList->aServers[j].nIP == nIP && pList->aServers[j].nPort == nPort)
			{
				bDuplicate = LTTRUE;
				break;
			}
		}
		if (bDuplicate) continue;

		GServerImplementation* pServer = &pList->aServers[pList->nServers++];
		memset(pServer, 0, sizeof(*pServer) - sizeof(pServer->sData));
		pServer->sData[0] = '\0';
		pServer->nIP    = nIP;
		pServer->nPort  = nPort;
		pServer->nState = GSS_WAITING;

        const auto address=address_text(nIP);
        GSL_AddString(pServer,"hostaddr",address.c_str());
	}
}

// The master conversation, one step.
static void GSL_UpdateListXfer(GServerListImplementation* list,const char* game,const char* key) {
    bool done=false;
    if (list->hTCP->connected()) {
        if (list->bSentRequest && list->request_sent < list->request.size()) {
            list->request_sent+=list->hTCP->send(std::as_bytes(std::span(list->request.data(),list->request.size())).subspan(list->request_sent));
        }
        const auto space=GSL_LIST_BUFFER-1-list->nRecv;
        if (space <= 0) { GSL_Reset(list); return; }
        auto input=std::as_writable_bytes(std::span(list->aRecv+list->nRecv,std::size_t(space)));
        if (auto count=list->hTCP->receive(input)) {
            list->nRecv+=static_cast<int>(*count); list->aRecv[list->nRecv]=0;
            done=*count==0;
        }
    }
    if (!list->bSentRequest) {
        // This legacy protocol sends a six-character secure challenge without
        // a length prefix. Wait for all six across TCP read boundaries.
        char challenge[80];
        if (!done && nolf::gamespy::find_key(list->aRecv,"secure",challenge,sizeof(challenge)) && std::strlen(challenge)==6) {
            char validation[96]; nolf::gamespy::validate(challenge,key,validation);
            char request[512];
            std::snprintf(request,sizeof(request),
                "\\gamename\\%.64s\\enctype\\0\\validate\\%s\\final\\\\queryid\\1.1\\\\list\\cmp\\gamename\\%.64s\\final\\",game,validation,game);
            list->request=request; list->request_sent=0;
            list->bSentRequest=LTTRUE; list->nRecv=0; list->aRecv[0]=0;
        }
    } else if (list->nRecv>=7 && std::memcmp(list->aRecv+list->nRecv-7,"\\final\\",7)==0) done=true;
    if (milliseconds()-list->dwListStart > GSL_LIST_TIMEOUT) { GSL_Reset(list); return; }
    if (!done) return;
    if (list->bSentRequest && list->request_sent==list->request.size() &&
        !(list->nRecv>=7 && (std::memcmp(list->aRecv,"\\error\\",7)==0 || std::memcmp(list->aRecv,"\\fatal\\",7)==0))) {
        int size=list->nRecv;
        if (size>=7 && std::memcmp(list->aRecv+size-7,"\\final\\",7)==0) size-=7;
        if (size%6==0) GSL_TakeList(list,list->aRecv,size);
    }
    list->hTCP.reset();
    if (list->nServers>0) {
        list->hUDP=std::make_unique<nolf::platform::UdpSocket>(); list->nState=sl_querying;
    } else GSL_Reset(list);
}

static void GSL_SendQuery(GServerListImplementation* pList, GServerImplementation* pServer)
{
    static constexpr char query[]="\\status\\";
    pList->hUDP->send({address_text(pServer->nIP),pServer->nPort},std::as_bytes(std::span(query,sizeof(query)-1)));

	pServer->dwLastSend = milliseconds();
	if (!pServer->nTries) pServer->dwSent = pServer->dwLastSend;
	pServer->nTries++;
}

static void GSL_Finish(GServerListImplementation* pList, GServerImplementation* pServer, LTBOOL bAnswered)
{
	pServer->nState = bAnswered ? GSS_DONE : GSS_FAILED;
	pList->nActive--;
	pList->nFinished++;

	if (bAnswered && pList->nListed < GSL_MAX_SERVERS)
	{
		char sPing[16];
		sprintf(sPing, "%d", pServer->nPing > 0 ? pServer->nPing : 1);
		GSL_AddString(pServer, "ping", sPing);
		pList->apListed[pList->nListed++] = pServer;
	}
}

// The server queries, one step. LTTRUE when a server was added to the list.
static LTBOOL GSL_UpdateQueries(GServerListImplementation* pList)
{
	LTBOOL  bNew  = LTFALSE;
	std::uint64_t dwNow = milliseconds();
	int   i;

    // Bound receive work so a busy network cannot starve the UI frame.
    for (int packet_index=0;packet_index<128;++packet_index) {
        const auto packet=pList->hUDP->receive(std::chrono::milliseconds(0));
        if (!packet) break;
        if (packet->bytes.empty() || packet->bytes.size()>1499) continue;
        std::string text(reinterpret_cast<const char*>(packet->bytes.data()),packet->bytes.size());
        if (text.find('\0')!=text.npos) continue;
        for (i=0;i<pList->nServers;++i) {
            auto* server=&pList->aServers[i];
            if (server->nState!=GSS_QUERYING || server->nPort!=packet->peer.port || address_text(server->nIP)!=packet->peer.address) continue;
            if (!server->nPacketsGot) server->nPing=static_cast<int>(milliseconds()-server->dwSent);
            GSL_ParseReply(server,text.c_str(),static_cast<int>(text.size()));
            if (GSL_ReplyComplete(server)) { GSL_Finish(pList,server,LTTRUE); bNew=LTTRUE; }
            break;
        }
    }

	// timeouts and resends
	for (i = 0; i < pList->nServers; i++)
	{
		GServerImplementation* pServer = &pList->aServers[i];
		if (pServer->nState != GSS_QUERYING) continue;
		if ((dwNow - pServer->dwLastSend) < GSL_QUERY_RETRY) continue;

        if (pServer->nTries >= GSL_QUERY_TRIES) GSL_Finish(pList,pServer,LTFALSE);
        else {
            // A retry is a new response transaction. Never count duplicates
            // or a partial reply as a complete server listing.
            pServer->packet_mask=0; pServer->query_id=0; pServer->nPacketsGot=0; pServer->nPackets=0;
            pServer->nPairs=0; pServer->nDataUsed=0;
            const auto address=address_text(pServer->nIP); GSL_AddString(pServer,"hostaddr",address.c_str());
            GSL_SendQuery(pList,pServer);
        }
	}

	// new queries
	while (pList->nActive < GSCM_MAX_UPDATES && pList->nNextQuery < pList->nServers)
	{
		GServerImplementation* pServer = &pList->aServers[pList->nNextQuery++];
		pServer->nState = GSS_QUERYING;
		pList->nActive++;
		GSL_SendQuery(pList, pServer);
	}

	if (pList->nFinished >= pList->nServers)
	{
		GSL_CloseSockets(pList);
		pList->nState = sl_idle;
	}

	return(bNew);
}

static LTBOOL GSL_IsListed(GServerListImplementation* pList, void* pHandle)
{
	if (!pList || !pHandle) return(LTFALSE);
	for (int i = 0; i < pList->nListed; i++)
	{
		if ((void*)pList->apListed[i] == pHandle) return(LTTRUE);
	}
	return(LTFALSE);
}


// --- sorting ------------------------------------------------------------- //

static const char*  s_sSortKey  = "";
static GCompareMode s_eSortMode = cm_stricase;
static LTBOOL         s_bSortUp   = LTTRUE;

static int GSL_Compare(const void* pA, const void* pB)
{
	char* sA = GSL_GetValue(*(GServerImplementation**)pA, s_sSortKey);
	char* sB = GSL_GetValue(*(GServerImplementation**)pB, s_sSortKey);
	if (!sA) sA = "";
	if (!sB) sB = "";

	int nCmp;
	switch (s_eSortMode)
	{
		case cm_int:
		{
			int a = atoi(sA), b = atoi(sB);
			nCmp = (a < b) ? -1 : ((a > b) ? 1 : 0);
			break;
		}
		case cm_float:
		{
			double a = atof(sA), b = atof(sB);
			nCmp = (a < b) ? -1 : ((a > b) ? 1 : 0);
			break;
		}
		case cm_strcase: nCmp = strcmp(sA, sB);  break;
		default:         nCmp = nolf::ascii_compare(sA, sB); break;
	}

	return(s_bSortUp ? nCmp : -nCmp);
}


// --- CGameSpyClientMgr ---------------------------------------------------- //

LTBOOL CGameSpyClientMgr::Init(const char* sGameName, const char* sSecretKey)
{
	Term();

	strncpy(m_sGameName, sGameName ? sGameName : "", GSCM_MAX_STRING - 1);
	m_sGameName[GSCM_MAX_STRING - 1] = '\0';
	strncpy(m_sSecretKey, sSecretKey ? sSecretKey : "", GSCM_MAX_STRING - 1);
	m_sSecretKey[GSCM_MAX_STRING - 1] = '\0';


	if (!CreateServerList()) return(LTFALSE);

	m_bInit = LTTRUE;
	return(LTTRUE);
}

void CGameSpyClientMgr::Term()
{
	FreeServerList();
	Clear();
}

void CGameSpyClientMgr::Clear()
{
	m_bInit           = LTFALSE;
	m_pServerList     = NULL;
	m_sGameName[0]    = '\0';
	m_sSecretKey[0]   = '\0';
	m_nGetServerIndex = 0;
	m_nProgress       = 0;
	m_bNewServer      = LTFALSE;
}

LTBOOL CGameSpyClientMgr::CreateServerList()
{
    auto* pList=new(std::nothrow) GServerListImplementation{};
    if (!pList) return LTFALSE;
    GSL_Reset(pList);

	m_pServerList = pList;
	return(LTTRUE);
}

void CGameSpyClientMgr::FreeServerList()
{
	if (m_pServerList)
	{
		GSL_Reset(m_pServerList);
		delete m_pServerList;
	}
	m_pServerList = NULL;
}

int CGameSpyClientMgr::GetNumServers()
{
	return(m_pServerList ? m_pServerList->nListed : 0);
}

int CGameSpyClientMgr::GetState()
{
	return(m_pServerList ? m_pServerList->nState : sl_idle);
}

void CGameSpyClientMgr::ClearServers()
{
	if (m_pServerList) GSL_Reset(m_pServerList);
	m_nGetServerIndex = 0;
	m_bNewServer      = LTFALSE;
}

LTBOOL CGameSpyClientMgr::RefreshServers(LTBOOL bAsync)
{
	if (!m_bInit || !m_pServerList) return(LTFALSE);

	ClearServers();
	m_nProgress = 100;

	GServerListImplementation* pList = m_pServerList;

    std::string master=*s_sMasterServer ? s_sMasterServer : "master.openspy.net";
    if (nolf::ascii_compare(master,"none")==0) return LTTRUE;
    std::uint16_t port=28900;
    if (const auto delimiter=master.find(':'); delimiter!=master.npos) {
        unsigned parsed=0;
        const auto value=std::string_view(master).substr(delimiter+1);
        const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
        if (result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !parsed || parsed>65535) return LTFALSE;
        port=static_cast<std::uint16_t>(parsed); master.resize(delimiter);
    }
    try { pList->hTCP=std::make_unique<nolf::platform::TcpSocket>(nolf::platform::Endpoint{master,port}); }
    catch (const std::exception& error) { nolf::platform::log("server browser",error.what()); GSL_Reset(pList); return LTFALSE; }

	pList->dwListStart = milliseconds();
	pList->nState      = sl_listxfer;
	m_nProgress        = 0;

	// The synchronous form: run the whole refresh now.
	if (!bAsync)
	{
		while (pList->nState != sl_idle)
		{
			Update();
			nolf::platform::sleep_for(std::chrono::milliseconds(10));
		}
	}

	return(LTTRUE);
}

CGameSpyServer* CGameSpyClientMgr::GetServer(int nIndex)
{
	if (!m_pServerList || nIndex < 0 || nIndex >= m_pServerList->nListed) return(NULL);
	return(CreateGameSpyServer(m_pServerList->apListed[nIndex]));
}

CGameSpyServer* CGameSpyClientMgr::GetServerFromHandle(void* pHandle)
{
	if (!GSL_IsListed(m_pServerList, pHandle)) return(NULL);
	return(CreateGameSpyServer((GServer)pHandle));
}

CGameSpyServer* CGameSpyClientMgr::GetFirstServer()
{
	m_nGetServerIndex = 0;
	return(GetServer(m_nGetServerIndex));
}

CGameSpyServer* CGameSpyClientMgr::GetNextServer()
{
	m_nGetServerIndex++;
	return(GetServer(m_nGetServerIndex));
}

LTBOOL CGameSpyClientMgr::ExistNewServer()
{
	LTBOOL bNew = m_bNewServer;
	m_bNewServer = LTFALSE;
	return(bNew);
}

void CGameSpyClientMgr::SortServersByName(LTBOOL bAscending)     { SortServers(bAscending, "hostname", cm_stricase); }
void CGameSpyClientMgr::SortServersByPing(LTBOOL bAscending)     { SortServers(bAscending, "ping", cm_int); }
void CGameSpyClientMgr::SortServersByPlayers(LTBOOL bAscending)  { SortServers(bAscending, "numplayers", cm_int); }
void CGameSpyClientMgr::SortServersByMap(LTBOOL bAscending)      { SortServers(bAscending, "mapname", cm_stricase); }
void CGameSpyClientMgr::SortServersByGameType(LTBOOL bAscending) { SortServers(bAscending, "gametype", cm_stricase); }

void CGameSpyClientMgr::SortServers(LTBOOL bAscending, char* sSortKey, GCompareMode eCompareMode)
{
	if (!m_pServerList || m_pServerList->nListed < 2 || !sSortKey) return;

	s_sSortKey  = sSortKey;
	s_eSortMode = eCompareMode;
	s_bSortUp   = bAscending;
	qsort(m_pServerList->apListed, m_pServerList->nListed, sizeof(GServerImplementation*), GSL_Compare);
}

void CGameSpyClientMgr::Update()
{
	GServerListImplementation* pList = m_pServerList;
	if (!m_bInit || !pList) return;

    try {
	switch (pList->nState)
	{
		case sl_listxfer:
			GSL_UpdateListXfer(pList, m_sGameName, m_sSecretKey);
			break;

		case sl_querying:
			if (GSL_UpdateQueries(pList)) m_bNewServer = LTTRUE;
			m_nProgress = pList->nServers ? (pList->nFinished * 100 / pList->nServers) : 100;
			break;
	}
    } catch (const std::exception& error) { nolf::platform::log("server browser",error.what()); GSL_Reset(pList); }
    if (pList->nState==sl_idle) m_nProgress=100;
}

void CGameSpyClientMgr::HandleCallback(GServerList pServerList, int nMsg, void* pParam1, void* pParam2)
{
	if (nMsg == LIST_PROGRESS)
	{
		m_nProgress = static_cast<int>(reinterpret_cast<std::intptr_t>(pParam2));
		m_bNewServer = LTTRUE;
	}
}

CGameSpyServer* CGameSpyClientMgr::CreateGameSpyServer(GServer pGServer)
{
	if (!pGServer) return(NULL);
	if (!m_CurServer.Init(pGServer)) return(NULL);
	return(&m_CurServer);
}



// ----------------------------------------------------------------------- //
//  CGameSpyServer -- one browser row. Never handed a server (see above).
// ----------------------------------------------------------------------- //

LTBOOL CGameSpyServer::Init(GServer pServer)
{
	Term();
	if (!pServer) return(LTFALSE);
	m_pGServer = pServer;
	m_bInit    = LTTRUE;
	return(LTTRUE);
}

void CGameSpyServer::Term()
{
	for (int i = 0; i < m_nNumPlayers; i++) m_aPlayers[i].Term();
	Clear();
}

void CGameSpyServer::Clear()
{
	m_pGServer     = NULL;
	m_bInit        = LTFALSE;
	m_nNumPlayers  = 0;
	m_nPlayerIndex = 0;
}

char* CGameSpyServer::GetName()       { return(GetStringValue("hostname")); }
char* CGameSpyServer::GetGameType()   { return(GetStringValue("gametype")); }
char* CGameSpyServer::GetMap()        { return(GetStringValue("mapname")); }
int   CGameSpyServer::GetPing()       { return(GetIntValue("ping", 9999)); }
int   CGameSpyServer::GetNumPlayers() { return(GetIntValue("numplayers")); }
int   CGameSpyServer::GetMaxPlayers() { return(GetIntValue("maxplayers")); }
char* CGameSpyServer::GetAddress()    { return(GetStringValue("hostaddr")); }

// M-P6-02: the server's answer to \status\ (and "hostaddr", "ping")
char* CGameSpyServer::GetStringValue(char* sKey, char* sDefault)
{
	char* sValue = GSL_GetValue(m_pGServer, sKey);
	return(sValue ? sValue : sDefault);
}

int CGameSpyServer::GetIntValue(char* sKey, int nDefault)
{
	char* sValue = GSL_GetValue(m_pGServer, sKey);
	return(sValue ? atoi(sValue) : nDefault);
}

float CGameSpyServer::GetFloatValue(char* sKey, float fDefault)
{
	char* sValue = GSL_GetValue(m_pGServer, sKey);
	return(sValue ? (float)atof(sValue) : fDefault);
}

int CGameSpyServer::UpdatePlayers()
{
	for (int i = 0; i < m_nNumPlayers; i++) m_aPlayers[i].Term();
	m_nNumPlayers  = 0;
	m_nPlayerIndex = 0;

	if (!m_bInit) return(0);

	int nPlayers = GetNumPlayers();
	if (nPlayers > GSS_MAX_PLAYERS) nPlayers = GSS_MAX_PLAYERS;

	for (int j = 0; j < nPlayers; j++)
	{
		if (m_aPlayers[m_nNumPlayers].Init(m_pGServer, j)) m_nNumPlayers++;
	}

	return(m_nNumPlayers);
}

CGameSpyPlayer* CGameSpyServer::GetPlayer(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_nNumPlayers) return(NULL);
	return(&m_aPlayers[nIndex]);
}

CGameSpyPlayer* CGameSpyServer::GetFirstPlayer()
{
	m_nPlayerIndex = 0;
	return(GetPlayer(m_nPlayerIndex));
}

CGameSpyPlayer* CGameSpyServer::GetNextPlayer()
{
	m_nPlayerIndex++;
	return(GetPlayer(m_nPlayerIndex));
}

void CGameSpyServer::SortPlayers(LTBOOL bAscending, int nKey)
{
	// insertion sort by the requested key
	for (int i = 1; i < m_nNumPlayers; i++)
	{
		for (int j = i; j > 0; j--)
		{
			CGameSpyPlayer* a = &m_aPlayers[j - 1];
			CGameSpyPlayer* b = &m_aPlayers[j];
			int nCmp;
			switch (nKey)
			{
				case GSS_SORT_PING:  nCmp = a->GetPing() - b->GetPing(); break;
				case GSS_SORT_FRAGS: nCmp = a->GetFrags() - b->GetFrags(); break;
				default:             nCmp = nolf::ascii_compare(a->GetName(), b->GetName()); break;
			}
			if (!bAscending) nCmp = -nCmp;
			if (nCmp <= 0) break;

			char tmp[sizeof(CGameSpyPlayer)];
			memcpy(tmp, a, sizeof(tmp));
			memcpy(a, b, sizeof(tmp));
			memcpy(b, tmp, sizeof(tmp));
		}
	}
}


// ----------------------------------------------------------------------- //
//  CGameSpyPlayer
// ----------------------------------------------------------------------- //

LTBOOL CGameSpyPlayer::Init(GServer pGServer, int nIndex)
{
	Term();
	if (!pGServer) return(LTFALSE);
	m_pGServer = pGServer;
	m_nIndex   = nIndex;
	m_bInit    = LTTRUE;
	return(LTTRUE);
}

void CGameSpyPlayer::Term()
{
	Clear();
}

void CGameSpyPlayer::Clear()
{
	m_pGServer = NULL;
	m_nIndex   = 0;
	m_bInit    = LTFALSE;
}

char* CGameSpyPlayer::GetName()  { return(GetStringValue("player")); }
int   CGameSpyPlayer::GetFrags() { return(GetIntValue("frags")); }
int   CGameSpyPlayer::GetPing()  { return(GetIntValue("ping")); }

// M-P6-02: a player's keys are the server's "<key>_<index>"
static char* GSL_GetPlayerValue(GServer pServer, char* sKey, int nIndex)
{
	char sPlayerKey[GSL_MAX_KEY + 16];
	if (!sKey || strlen(sKey) > GSL_MAX_KEY) return(NULL);
	sprintf(sPlayerKey, "%s_%d", sKey, nIndex);
	return(GSL_GetValue(pServer, sPlayerKey));
}

char* CGameSpyPlayer::GetStringValue(char* sKey, char* sDefault)
{
	char* sValue = GSL_GetPlayerValue(m_pGServer, sKey, m_nIndex);
	return(sValue ? sValue : sDefault);
}

int CGameSpyPlayer::GetIntValue(char* sKey, int nDefault)
{
	char* sValue = GSL_GetPlayerValue(m_pGServer, sKey, m_nIndex);
	return(sValue ? atoi(sValue) : nDefault);
}

float CGameSpyPlayer::GetFloatValue(char* sKey, float fDefault)
{
	char* sValue = GSL_GetPlayerValue(m_pGServer, sKey, m_nIndex);
	return(sValue ? (float)atof(sValue) : fDefault);
}
