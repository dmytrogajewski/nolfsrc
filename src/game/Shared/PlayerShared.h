// ----------------------------------------------------------------------- //
//
// MODULE  : PlayerShared.h
//
// PURPOSE : Types and globals shared between the client and the server
//			 associated with the player.
//
// CREATED : 09/06/00
//
// (c) 2000 Monolith Productions, Inc.  All Rights Reserved
//
// ----------------------------------------------------------------------- //

#ifndef __PLAYER_SHARED_H__
#define __PLAYER_SHARED_H__

#include "ltvector.h"
#include "engine/portable/string.h"
#include "ModelButeMgr.h"

// Shared player stuff...

const LTVector g_kvPlayerCameraOffset(0.0, 41.0, 0.0);
const LTVector g_kvPlayerScubaCameraOffset(0.0, 30.0, 0.0);

inline LTVector GetPlayerHeadOffset(CModelButeMgr* pMBMgr, ModelStyle eStyle) 
{
	// Always use normal offsets in multiplayer...

	if (!IsMultiplayerGame())
	{
		if (pMBMgr)
		{
			if (nolf::ascii_compare(pMBMgr->GetStyleName(eStyle), "Scuba") == 0)
			{
				return g_kvPlayerScubaCameraOffset;
			}
		}
	}

	return g_kvPlayerCameraOffset; 
}

#endif // __PLAYER_SHARED_H__