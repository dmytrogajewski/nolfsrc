// ----------------------------------------------------------------------- //
//
// MODULE  : p2_bighead.cpp
//
// PURPOSE : M-P2-10, big head mode -- the client half (NOLFSRC_MODERN only)
//
// ----------------------------------------------------------------------- //
//
// After NOLF Modernizer's big head mode (credit there; MODERN.md, P2); our
// own code. See p2_bighead.h.

#include "Stdafx.h"
#include "engine/portable/string.h"
#include "GameClientShell.h"
#include "VarTrack.h"
#include "MsgIDs.h"
#include "cshell/p2_bighead.h"

extern CGameClientShell* g_pGameClientShell;

#define P2BIGHEAD_BASE		1.8f			// the scale at BigHeadMode 1
#define P2BIGHEAD_NODE		"head_node"		// every NOLF character skeleton's head

namespace
{
	VarTrack s_vtBigHeadMode;
}

void P2BigHead_Init()
{
	s_vtBigHeadMode.Init(g_pLTClient, "BigHeadMode", LTNULL, 0.0f);
}

LTFLOAT P2BigHead_Factor()
{
	LTFLOAT fScale;

	if (!s_vtBigHeadMode.IsInitted() || !g_pGameClientShell) return 0.0f;

	// Everyone in a multiplayer game must see the same targets.
	if (g_pGameClientShell->IsMultiplayerGame()) return 0.0f;

	fScale = s_vtBigHeadMode.GetFloat();
	if (fScale <= 0.0f) return 0.0f;

	return fScale * P2BIGHEAD_BASE;
}

void P2BigHead_ScaleNode(HOBJECT hObj, HMODELNODE hNode, LTMatrix* pGlobalMat)
{
	char    szName[32];
	LTFLOAT fFactor;

	if (!pGlobalMat) return;

	fFactor = P2BigHead_Factor();
	if (fFactor <= 0.0f) return;

	// By name, every time: cheap next to drawing the model, and a handle
	// cached per object could outlive the object it was found on. The
	// engine copies at most sizeof - 1 characters and never terminates
	// (RETAIL_BUGS.md, ci_GetModelNodeName): the last byte is ours.
	szName[sizeof(szName) - 1] = '\0';
	if (g_pLTClient->GetModelNodeName(hObj, hNode, szName, sizeof(szName)) != LT_OK)
		return;

	if (nolf::ascii_compare(szName, P2BIGHEAD_NODE) == 0)
		pGlobalMat->Scale(fFactor, fFactor, fFactor);
}

static void BodyNodeControlFn(HOBJECT hObj, HMODELNODE hNode, LTMatrix* pGlobalMat, void* pUserData)
{
	P2BigHead_ScaleNode(hObj, hNode, pGlobalMat);
}

void P2BigHead_UpdateBody(HOBJECT hBody, LTBOOL* pbInstalled)
{
	LTBOOL bWant;

	if (!hBody || !pbInstalled) return;

	bWant = (P2BigHead_Factor() > 0.0f);
	if (bWant == *pbInstalled) return;

	g_pLTClient->ModelNodeControl(hBody, bWant ? BodyNodeControlFn : LTNULL, LTNULL);
	*pbInstalled = bWant;
}

void P2BigHead_SendToServer()
{
	char    szCmd[64];
	HSTRING hCmd;

	if (!s_vtBigHeadMode.IsInitted() || !g_pGameClientShell || !g_pLTClient->IsConnected()) return;
	if (g_pGameClientShell->IsMultiplayerGame() && !g_pGameClientShell->IsHosting()) return;

	// The same message as a line typed into the console (M-P2-08).
	sprintf(szCmd, "BigHeadMode %f", s_vtBigHeadMode.GetFloat());

	hCmd = g_pLTClient->CreateString(szCmd);
	if (!hCmd) return;

	HMESSAGEWRITE hMsg = g_pLTClient->StartMessage(MID_CONSOLE_COMMAND_CLIENT);
	g_pLTClient->WriteToMessageHString(hMsg, hCmd);
	g_pLTClient->EndMessage(hMsg);
	g_pLTClient->FreeString(hCmd);
}
