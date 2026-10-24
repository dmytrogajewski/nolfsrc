// ----------------------------------------------------------------------- //
//
// MODULE  : p2_server.cpp
//
// PURPOSE : P2's server half (NOLFSRC_MODERN only) -- see p2_server.h
//
// ----------------------------------------------------------------------- //
//
// After NOLF Modernizer's client-to-server console command, big head mode
// and trigger display (credit there; MODERN.md, P2); our own code. One
// difference by design: the Modernizer's server runs a console string from
// any client, ours only from the local one (M-P2-08).

#include "Stdafx.h"
#include "engine/portable/string.h"
#include "GameServerShell.h"
#include "GameBase.h"
#include "PlayerObj.h"
#include "CVarTrack.h"
#include "cshell/p2_log.h"
#include "object/p2_server.h"

#define P2BIGHEAD_BASE		1.8f		// the scale at BigHeadMode 1 (as the client)
#define P2CONSOLE_CMD_LEN	256

namespace
{
	CVarTrack	s_vtBigHeadMode;		// M-P2-10: set from the client
	CVarTrack	s_vtDisplayTriggers;	// M-P2-11
	LTBOOL		s_bTriggersShown = LTFALSE;
	LTBOOL		s_bHeadsScaled = LTFALSE;	// some attachment was scaled this run
	HCLASS		s_hTriggerClass = LTNULL;
}

void P2Server_Init()
{
	if (!s_vtBigHeadMode.IsInitted())
		s_vtBigHeadMode.Init(g_pLTServer, "BigHeadMode", LTNULL, 0.0f);
	if (!s_vtDisplayTriggers.IsInitted())
		s_vtDisplayTriggers.Init(g_pLTServer, "DisplayTriggers", LTNULL, 0.0f);

	s_bTriggersShown = LTFALSE;
	s_hTriggerClass = LTNULL;
}


// ----------------------------------------------------------------------- //
//  M-P2-08
// ----------------------------------------------------------------------- //

void P2Server_OnConsoleCommand(HCLIENT hSender, HMESSAGEREAD hMessage)
{
	char    szCmd[P2CONSOLE_CMD_LEN];
	HSTRING hCmd;
	char*   pCmd;

	hCmd = g_pLTServer->ReadFromMessageHString(hMessage);
	if (!hCmd) return;

	pCmd = g_pLTServer->GetStringData(hCmd);
	strncpy(szCmd, pCmd ? pCmd : "", sizeof(szCmd) - 1);
	szCmd[sizeof(szCmd) - 1] = '\0';
	g_pLTServer->FreeString(hCmd);

	if (!szCmd[0]) return;

	// Only the player at this machine -- single player, or the host of a
	// listen server -- may run commands on the server. A remote client
	// (or a dedicated server, which has no local client) gets nothing.
	if (!hSender || !(g_pLTServer->GetClientInfoFlags(hSender) & CIF_LOCAL))
	{
		ModernLog("Ignored a console command from a remote client: %s", szCmd);
		return;
	}

	g_pLTServer->RunGameConString(szCmd);
}


// ----------------------------------------------------------------------- //
//  M-P2-14
// ----------------------------------------------------------------------- //

void P2Server_OnSetPos(HCLIENT hSender, HMESSAGEREAD hMessage)
{
	LTVector    vPos, vZero(0.0f, 0.0f, 0.0f);
	CPlayerObj* pPlayer;

	g_pLTServer->ReadFromMessageVector(hMessage, &vPos);

	if (IsMultiplayerGame() || !hSender ||
		!(g_pLTServer->GetClientInfoFlags(hSender) & CIF_LOCAL))
	{
		return;
	}

	pPlayer = (CPlayerObj*)g_pLTServer->GetClientUserData(hSender);
	if (!pPlayer || !pPlayer->m_hObject) return;

	g_pLTServer->TeleportObject(pPlayer->m_hObject, &vPos);
	g_pLTServer->SetVelocity(pPlayer->m_hObject, &vZero);
	pPlayer->TeleportClientToServerPos();
}


// ----------------------------------------------------------------------- //
//  M-P2-10
// ----------------------------------------------------------------------- //

LTFLOAT P2Server_BigHeadFactor()
{
	LTFLOAT fScale;

	if (!s_vtBigHeadMode.IsInitted() || IsMultiplayerGame()) return 1.0f;

	fScale = s_vtBigHeadMode.GetFloat();
	if (fScale <= 0.0f) return 1.0f;

	return fScale * P2BIGHEAD_BASE;
}

void P2Server_ScaleHeadAttachment(const char* pPositionName, HOBJECT hModel)
{
	LTFLOAT  fFactor;
	LTVector vScale;

	if (!pPositionName || !hModel) return;

	if (nolf::ascii_compare(pPositionName, "Head") != 0 &&
	    nolf::ascii_compare(pPositionName, "Eyes") != 0 &&
	    nolf::ascii_compare(pPositionName, "Chin") != 0)
	{
		return;
	}

	fFactor = P2Server_BigHeadFactor();

	// Back to normal size only if big head mode ever scaled something:
	// otherwise this leaves the retail game's attachments alone.
	if (fFactor == 1.0f && !s_bHeadsScaled) return;
	if (fFactor != 1.0f) s_bHeadsScaled = LTTRUE;

	vScale.Init(fFactor, fFactor, fFactor);
	g_pLTServer->ScaleObject(hModel, &vScale);
}


// ----------------------------------------------------------------------- //
//  M-P2-11
// ----------------------------------------------------------------------- //

static LTBOOL IsTrigger(GameBase* pObj)
{
	if (!pObj || !pObj->m_hObject) return LTFALSE;

	if (!s_hTriggerClass)
		s_hTriggerClass = g_pLTServer->GetClass("Trigger");

	return s_hTriggerClass &&
	       g_pLTServer->IsKindOf(g_pLTServer->GetObjectClass(pObj->m_hObject), s_hTriggerClass);
}

LTBOOL P2Server_ShowTriggerBox(GameBase* pObj)
{
	return s_vtDisplayTriggers.GetFloat() != 0.0f && IsTrigger(pObj);
}

LTBOOL P2Server_TriggerBoxColor(GameBase* pObj, LTVector* pColor, LTFLOAT* pAlpha)
{
	if (!P2Server_ShowTriggerBox(pObj)) return LTFALSE;

	pColor->Init(1.0f, 1.0f, 0.0f);		// yellow, nearly opaque
	*pAlpha = 0.95f;
	return LTTRUE;
}

// Every Trigger, active or not, gets its box updated (shown) or removed.
static void ForEachTrigger(LTBOOL bShow)
{
	HOBJECT hObj;
	int     nPass;

	if (!s_hTriggerClass)
		s_hTriggerClass = g_pLTServer->GetClass("Trigger");
	if (!s_hTriggerClass) return;

	for (nPass = 0; nPass < 2; nPass++)
	{
		hObj = nPass ? g_pLTServer->GetNextInactiveObject(LTNULL)
		             : g_pLTServer->GetNextObject(LTNULL);
		while (hObj)
		{
			if (g_pLTServer->IsKindOf(g_pLTServer->GetObjectClass(hObj), s_hTriggerClass))
			{
				GameBase* pObj = (GameBase*)g_pLTServer->HandleToObject(hObj);
				if (pObj)
				{
					if (bShow)
						pObj->UpdateBoundingBox();
					else
						pObj->RemoveBoundingBox();
				}
			}

			hObj = nPass ? g_pLTServer->GetNextInactiveObject(hObj)
			             : g_pLTServer->GetNextObject(hObj);
		}
	}
}

void P2Server_Update()
{
	LTBOOL bShow = s_vtDisplayTriggers.IsInitted() && s_vtDisplayTriggers.GetFloat() != 0.0f;

	// Shown: keep them up to date (triggers move and resize). Just turned
	// off: take them away once.
	if (bShow)
		ForEachTrigger(LTTRUE);
	else if (s_bTriggersShown)
		ForEachTrigger(LTFALSE);

	s_bTriggersShown = bShow;
}
