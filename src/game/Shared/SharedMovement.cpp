// ----------------------------------------------------------------------- //
//
// MODULE  : SharedMovement.cpp
//
// PURPOSE : Shared movement implementation.
//
// CREATED : 8/27/99
//
// (c)	1999 Monolith Productions, Inc.  All Rights Reserved
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include "engine/portable/string.h"
#include "SharedMovement.h"

#if !defined(_CLIENTBUILD)

char* aPPMStrings[] =
{
	"Normal",
	"Motorcycle",
	"Snowmobile"
};

char* GetPropertyNameFromPlayerPhysicsModel(PlayerPhysicsModel ePPModel)
{
	if (PPM_FIRST <= ePPModel && ePPModel < PPM_NUM_MODELS)
	{
		return aPPMStrings[ePPModel];
	}

    return LTNULL;
}


PlayerPhysicsModel GetPlayerPhysicsModelFromPropertyName(char* pName)
{
	if (!pName) return PPM_NORMAL;

	for (int i = 0; i < PPM_NUM_MODELS; i++)
	{
		if (nolf::ascii_compare(aPPMStrings[i], pName) == 0)
		{
			return (PlayerPhysicsModel)i;
		}
	}

	return PPM_NORMAL;
}

#endif // ! _CLIENTBUILD