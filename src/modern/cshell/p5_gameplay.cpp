// ----------------------------------------------------------------------- //
//
// MODULE  : p5_gameplay.cpp
//
// PURPOSE : The modern build's gameplay and crash fixes and features in
//           CShell.dll (package P5 of MODERN.md). Compiled only with
//           NOLFSRC_MODERN.
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include <stdio.h>
#include "GameClientShell.h"
#include "InterfaceMgr.h"
#include "PlayerStats.h"
#include "WeaponModel.h"
#include "WeaponMgr.h"
#include "ModelButeMgr.h"
#include "ClientUtilities.h"
#include "CommonUtilities.h"
#include "CommandIDs.h"
#include "VarTrack.h"
#include "cshell/p5_gameplay.h"
#include "cshell/p5_movement.h"
#include "CMoveMgr.h"
#include "VehicleMgr.h"

extern CGameClientShell* g_pGameClientShell;

// QuickSwitch (M-P5-12): next/previous weapon change the weapon at once.
static VarTrack s_vtQuickSwitch;

// CrouchToggle (M-P5-19): the Duck control toggles crouching.
static VarTrack s_vtCrouchToggle;

// How long the weapon chooser stays up after a quick switch.
static const float kfQuickSwitchChooserTime = 2.5f;

// ----------------------------------------------------------------------- //

void P5_OnEngineInitialized()
{
	if (!s_vtQuickSwitch.IsInitted())
	{
		s_vtQuickSwitch.Init(g_pLTClient, "QuickSwitch", NULL, 0.0f);
	}

	if (!s_vtCrouchToggle.IsInitted())
	{
		s_vtCrouchToggle.Init(g_pLTClient, "CrouchToggle", NULL, 0.0f);
	}

	// M-P5-13: the engine only binds actions it knows. It writes the ones it
	// knows into autoexec.cfg, so after the first run this repeats what the
	// config already said.
	char szCmd[128];
	sprintf(szCmd, "AddAction %s %d", CommandName(COMMAND_ID_TOGGLE_ZOOM), COMMAND_ID_TOGGLE_ZOOM);
	g_pLTClient->RunConsoleString(szCmd);
}

// ----------------------------------------------------------------------- //
//
//	M-P5-05: standing on the world across seams
//
// ----------------------------------------------------------------------- //

// How long the last surface is carried, at most -- ten frames at the 60
// frames a second this was tuned at, a time since M-P1-23, so it is the
// same at 300 -- and how far the player may have dropped meanwhile
// (UpdateOnGround counts more than 1 unit as a fall).
static const LTFLOAT	kfP5MaxStandingOnTime	= 10.0f / 60.0f;
static const LTFLOAT	kfP5MaxStandingOnDrop	= 1.0f;

void P5_ResetStandingOn(P5StandingOn & s)
{
	memset(&s.m_Info, 0, sizeof(s.m_Info));
	s.m_bValid	= LTFALSE;
	s.m_fY		= 0.0f;
	s.m_fCarried = 0.0f;
}

void P5_CarryStandingOn(P5StandingOn & s, CollisionInfo & Info, const LTVector & vPos, LTBOOL bAirborne)
{
	if (Info.m_hObject)
	{
		// Only the world is carried: it cannot go away in the meantime,
		// as a door or lift object could.
		s.m_bValid	= IsMainWorld(Info.m_hObject);
		s.m_Info	= Info;
		s.m_fY		= vPos.y;
		s.m_fCarried = 0.0f;
		return;
	}

	if (!s.m_bValid || bAirborne || s.m_fCarried >= kfP5MaxStandingOnTime ||
		s.m_fY - vPos.y > kfP5MaxStandingOnDrop)
	{
		s.m_bValid = LTFALSE;
		return;
	}

	s.m_fCarried += g_pGameClientShell->GetFrameTime();
	Info = s.m_Info;
}

// ----------------------------------------------------------------------- //
//
//	M-P5-06: an invalid model style
//
// ----------------------------------------------------------------------- //

ModelStyle P5_ValidModelStyle(ModelStyle ms)
{
	if (!g_pModelButeMgr) return ms;

	if (ms == eModelStyleInvalid || (int)ms >= g_pModelButeMgr->GetNumStyles())
	{
		return eModelStyleDefault;
	}

	return ms;
}

// ----------------------------------------------------------------------- //
//
//	M-P5-09: console strings without a copy
//
// ----------------------------------------------------------------------- //

const char* P5_GetConsoleStr(char* szKey)
{
	if (g_pLTClient)
	{
		HCONSOLEVAR hVar = g_pLTClient->GetConsoleVar(szKey);
		if (hVar)
		{
			const char* szValue = g_pLTClient->GetVarValueString(hVar);
			if (szValue) return szValue;
		}
	}

	return "";
}

// ----------------------------------------------------------------------- //
//
//	M-P5-10: the ammo HUD surface
//
//	It is as wide as a full clip of ammo icons, and the camera disabler's
//	clip is 10000 shots. It is drawn right-aligned at the ammo position,
//	so nothing beyond the screen's width can ever show.
//
// ----------------------------------------------------------------------- //

LTBOOL P5_FitAmmoHUD(int & nWidth, int nHeight, const LTRect & rcHUD)
{
	uint32 dwScreenWidth = 0, dwScreenHeight = 0;
	HSURFACE hScreen = g_pLTClient->GetScreenSurface();
	if (hScreen)
	{
		g_pLTClient->GetSurfaceDims(hScreen, &dwScreenWidth, &dwScreenHeight);
	}

	int nMax = (int)dwScreenWidth;
	if (nMax > 0 && nWidth > nMax)
	{
		nWidth = nMax;
	}

	if (nWidth > rcHUD.right || nHeight > rcHUD.bottom) return LTTRUE;

	// One made for a bigger clip (or a bigger screen) is made again.
	return (nMax > 0 && rcHUD.right > nMax) ? LTTRUE : LTFALSE;
}

// ----------------------------------------------------------------------- //
//
//	M-P5-12: quick switch
//
// ----------------------------------------------------------------------- //

LTBOOL P5_QuickSwitchOn()
{
	return (s_vtQuickSwitch.GetFloat() != 0.0f) ? LTTRUE : LTFALSE;
}

void P5_QuickSwitchSelect(uint8 nWeaponId)
{
	if (!P5_QuickSwitchOn() || !g_pGameClientShell || !g_pWeaponMgr) return;

	CWeaponModel* pWeaponModel = g_pGameClientShell->GetWeaponModel();
	if (!pWeaponModel || pWeaponModel->IsDisabled()) return;

	if (!g_pWeaponMgr->GetWeapon(nWeaponId)) return;

	// Change now, but leave the chooser up: it is how the player sees
	// where he is in the list while stepping through it.
	pWeaponModel->ChangeWeapon(g_pWeaponMgr->GetCommandId(nWeaponId), LTTRUE, LTTRUE);
}

float P5_ChooserDelay(float fDelay)
{
	return P5_QuickSwitchOn() ? kfQuickSwitchChooserTime : fDelay;
}

// ----------------------------------------------------------------------- //
//
//	M-P5-13: toggle zoom
//
// ----------------------------------------------------------------------- //

int P5_ToggleZoomCommand(int nZoomView)
{
	if (g_pGameClientShell && g_pWeaponMgr)
	{
		CPlayerStats* pStats = g_pGameClientShell->GetPlayerStats();
		if (pStats)
		{
			uint8 nScopeId = pStats->GetScope();
			if (nScopeId != WMGR_INVALID_ID)
			{
				MOD* pMod = g_pWeaponMgr->GetMod(nScopeId);
				if (pMod && nZoomView >= pMod->nZoomLevel)
				{
					return COMMAND_ID_ZOOM_OUT;
				}
			}
		}
	}

	return COMMAND_ID_ZOOM_IN;
}

// ----------------------------------------------------------------------- //
//
//	M-P5-14: model shadows after cinematics
//
//	TurnOnAlternativeCamera runs every frame a cinematic camera is live, and
//	each time saves MaxModelShadows before setting it to 0 -- so from the
//	second frame on it saves the 0, and the end of the cinematic "restores"
//	0. The display options set MaxModelShadows and DrawShadows together, so
//	DrawShadows is what it should come back to.
//
//	(Switched off until our d3d.ren's shadow pass stopped crashing: it
//	declared Direct3D's DrawPrimitive without __stdcall, so the call's
//	arguments were popped twice -- see w54_10048180_osoiqipsf.cpp. The
//	retail bug had been hiding that. 0 here gives the retail behaviour.)
//
// ----------------------------------------------------------------------- //

#define P5_RESTORE_MODEL_SHADOWS	1

extern int g_nCinSaveModelShadows;		// GameClientShell.cpp

static void P5_SetModelShadowsFromSetting()
{
	if (!g_pLTClient) return;

	HCONSOLEVAR hVar = g_pLTClient->GetConsoleVar("DrawShadows");
	if (!hVar) return;

	WriteConsoleInt("MaxModelShadows", (g_pLTClient->GetVarValueFloat(hVar) > 0.0f) ? 1 : 0);
}

void P5_OnCinematicCameraOff()
{
#if P5_RESTORE_MODEL_SHADOWS
	P5_SetModelShadowsFromSetting();
#else
	WriteConsoleInt("MaxModelShadows", g_nCinSaveModelShadows);
#endif
}

// M-P5-19: whether Duck toggles right now, as last told to the engine.
static LTBOOL s_bCrouchToggled;

static void P5_SetCrouchToggle(LTBOOL bToggle)
{
	s_bCrouchToggled = bToggle;
	g_pLTClient->SetCommandToggle(COMMAND_ID_DUCK, bToggle);	// also stands up
}

void P5_OnWorldStarted()
{
#if P5_RESTORE_MODEL_SHADOWS
	P5_SetModelShadowsFromSetting();
#endif

	// M-P5-19: every level starts standing
	P5_SetCrouchToggle(s_bCrouchToggled);
}

void P5_UpdateCrouchToggle()
{
	CMoveMgr* pMove;
	LTBOOL    bToggle;

	if (!g_pGameClientShell || !s_vtCrouchToggle.IsInitted()) return;

	pMove   = g_pGameClientShell->GetMoveMgr();
	bToggle = s_vtCrouchToggle.GetFloat() != 0.0f && pMove &&
	          !g_pGameClientShell->IsPlayerDead() &&
	          !g_pGameClientShell->IsSpectatorMode() &&
	          !pMove->IsFreeMovement() && !pMove->IsBodyInLiquid() &&
	          !pMove->IsBodyOnLadder() &&
	          !(pMove->GetVehicleMgr() && pMove->GetVehicleMgr()->IsVehiclePhysics());

	if (bToggle != s_bCrouchToggled)
		P5_SetCrouchToggle(bToggle);
}

// ----------------------------------------------------------------------- //
//
//	M-P5-18: the ammo each weapon was last used with
//
// ----------------------------------------------------------------------- //

// Indexed by weapon id (a uint8, so every id fits); 0 is "none yet", any
// other value the ammo id plus one.
static uint8 s_aLastAmmo[256];

void P5_RememberAmmo(uint8 nWeaponId, uint8 nAmmoId)
{
	if (nWeaponId == WMGR_INVALID_ID || nAmmoId == WMGR_INVALID_ID) return;

	s_aLastAmmo[nWeaponId] = (uint8)(nAmmoId + 1);
}

int P5_RememberedAmmo(uint8 nWeaponId)
{
	CPlayerStats* pStats = g_pGameClientShell ? g_pGameClientShell->GetPlayerStats() : LTNULL;
	WEAPON*       pWeapon = g_pWeaponMgr ? g_pWeaponMgr->GetWeapon(nWeaponId) : LTNULL;
	int           nAmmoId, i;

	if (!pStats || !pWeapon || !s_aLastAmmo[nWeaponId]) return WMGR_INVALID_ID;

	nAmmoId = s_aLastAmmo[nWeaponId] - 1;

	// Still one of the weapon's ammo types, and some of it left.
	for (i = 0; i < pWeapon->nNumAmmoTypes; i++)
	{
		if (pWeapon->aAmmoTypes[i] == nAmmoId)
		{
			return (pStats->GetAmmoCount((uint8)nAmmoId) > 0) ? nAmmoId : WMGR_INVALID_ID;
		}
	}

	return WMGR_INVALID_ID;
}
