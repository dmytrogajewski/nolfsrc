// *********************************************************************** //
//
//  MODULE:   GSMASTER (.H)  -- the modern build's GameSpy master protocol
//
//  PURPOSE:  M-P6-02. Shared by GameSpyMgr.cpp (a server reporting to the
//            master) and GameSpyClientMgr.cpp (the browser asking the
//            master for its list). Only the modern build (NOLFSRC_MODERN)
//            includes it; the release build's stand-ins talk to no master.
//
//  THE PROTOCOL (the GameSpy "version 1" master protocol of 1999-2000, as
//  still served by OpenSpy, master.openspy.net):
//
//  Server, UDP to master port 27900, from its query port (NOLF answers
//  queries on its game port):
//      -> \heartbeat\<queryport>\gamename\nolf             every minute
//      <- \secure\<challenge>                              first time only
//      -> \validate\<GSM_Validate(challenge, key)>\final\...
//      <- \status\                                         after each beat
//      -> the ordinary status reply (\hostname\...\final\)
//      -> \heartbeat\<queryport>\gamename\nolf\statechanged\2   on shutdown
//
//  Browser, TCP to master port 28900:
//      <- \basic\\secure\<challenge>
//      -> \gamename\nolf\gamever\<ver>\location\0\validate\<response>
//         \enctype\0\final\\queryid\1.1\\list\cmp\gamename\nolf\final\
//      <- 6 bytes per server (IPv4 address and port, both in network
//         byte order), then \final\, then the master closes.
//  then each server is asked \status\ over UDP directly, as on a LAN.
//
//  The validate response is the "secure" challenge-response every GameSpy
//  title used: an RC4-style keystream keyed by the game's secret key,
//  advanced by each challenge character, XORed into the challenge, and the
//  result written in base64. Written here from that description.
//
// *********************************************************************** //

#ifndef _GSMASTER_H_
#define _GSMASTER_H_

#ifdef NOLFSRC_MODERN

#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include "gs_protocol.h"
#include <string.h>

#pragma comment(lib, "wsock32.lib")

#define GSM_DEFAULT_MASTER		"master.openspy.net"
#define GSM_HEARTBEAT_PORT		27900
#define GSM_LIST_PORT			28900
#define GSM_DISABLED			"none"		// MasterServer value that turns the master off


// The browser's master address (host or host:port, "" for the default,
// "none" for no master). Kept by GameSpyClientMgr.cpp; set by the game
// before CGameSpyClientMgr::Init.
void GameSpyClient_SetMasterServer(const char* sAddr);


// GSM_Validate: the response to a master's challenge. sOut needs
// (strlen(sChallenge)+2)/3*4+1 bytes; challenges are at most 64 characters.
static void GSM_Validate(const char* challenge, const char* key, char* output) { nolf::gamespy::validate(challenge, key, output); }

// GSM_Resolve: "host" or "host:port" -> address in network order and port.
// Blocks while the name is looked up. FALSE if it cannot be resolved or is
// GSM_DISABLED.
static BOOL GSM_Resolve(const char* sAddr, int nDefaultPort, unsigned long* pIP, int* pPort)
{
	char sHost[128];

	if (!sAddr || !sAddr[0]) sAddr = GSM_DEFAULT_MASTER;
	if (stricmp(sAddr, GSM_DISABLED) == 0) return(FALSE);

	strncpy(sHost, sAddr, sizeof(sHost) - 1);
	sHost[sizeof(sHost) - 1] = '\0';

	*pPort = nDefaultPort;
	char* pColon = strchr(sHost, ':');
	if (pColon)
	{
		*pColon = '\0';
		int nPort = atoi(pColon + 1);
		if (nPort > 0 && nPort < 65536) *pPort = nPort;
	}

	unsigned long nIP = inet_addr(sHost);
	if (nIP == INADDR_NONE)
	{
		struct hostent* pHost = gethostbyname(sHost);
		if (!pHost || pHost->h_addrtype != AF_INET || !pHost->h_addr_list[0]) return(FALSE);
		memcpy(&nIP, pHost->h_addr_list[0], 4);
	}

	*pIP = nIP;
	return(TRUE);
}


// GSM_FindKey: the value of \key\ in a backslash-separated string, copied
// into sOut. FALSE if the key is not there.
static BOOL GSM_FindKey(const char* data, const char* key, char* output, int size) { return nolf::gamespy::find_key(data, key, output, size); }

#endif // NOLFSRC_MODERN

#endif // _GSMASTER_H_
