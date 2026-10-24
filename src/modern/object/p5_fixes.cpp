#include <cmath>
// ----------------------------------------------------------------------- //
//
// MODULE  : p5_fixes.cpp
//
// PURPOSE : The modern build's AI and object fixes in Object.lto
//           (package P5 of MODERN.md). Compiled only with NOLFSRC_MODERN.
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include <float.h>
#include <math.h>
#include "object/p5_fixes.h"

// ----------------------------------------------------------------------- //
//
//	M-P5-01: the dodge and cover movement tables
//
//	AIMovement.cpp's GetMovementData interpolates between afData[i] and
//	afData[i+1], i = int(fTime*cData). At fTime == 1.0 -- the frame the
//	animation ends -- i is cData, the last sample, and i+1 lies one past the
//	table. The weight of that sample is then 0, but 0 times a NaN is a NaN:
//	the word after s_afPistolCornerLeft in every retail Object.lto is
//	0xffffffff, so a pistol AI finishing a corner move was moved by a NaN
//	distance and vanished. Here the time is clamped and the last sample is
//	taken without its neighbour.
//
// ----------------------------------------------------------------------- //

static float P5_MovementSample(const float* afData, uint32 cData, float fTime)
{
	if (std::isnan(fTime) || fTime < 0.0f) fTime = 0.0f;
	if (fTime > 1.0f) fTime = 1.0f;

	float fPos = fTime * (float)cData;
	uint32 i = (uint32)fPos;
	if (i >= cData) return afData[cData];

	return afData[i] + (fPos - (float)i) * (afData[i + 1] - afData[i]);
}

LTFLOAT P5_GetMovementData(const float* afData, uint32 cData, float fTimePrev, float fTime)
{
	return (LTFLOAT)fabs(P5_MovementSample(afData, cData, fTime) -
						 P5_MovementSample(afData, cData, fTimePrev));
}

// ----------------------------------------------------------------------- //
//
//	M-P5-03, M-P5-04: vectors that cannot be normalised
//
// ----------------------------------------------------------------------- //

LTBOOL P5_IsNaN(const LTVector& v)
{
	return (std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z)) ? LTTRUE : LTFALSE;
}

LTBOOL P5_NormalizeIfNonZero(LTVector& v)
{
	if (P5_IsNaN(v)) return LTFALSE;

	float fMagSqr = v.x*v.x + v.y*v.y + v.z*v.z;
	if (!(fMagSqr > 0.0f)) return LTFALSE;

	float fInv = 1.0f / (float)sqrt(fMagSqr);
	v.x *= fInv;
	v.y *= fInv;
	v.z *= fInv;
	return LTTRUE;
}

LTBOOL P5_RepairLoadedDoor(LTVector& vMoveDir, LTVector& vOpenPos, const LTVector& vClosedPos, LTFLOAT fMoveDist)
{
	LTBOOL bRepaired = LTFALSE;

	if (P5_IsNaN(vMoveDir))
	{
		// It was a zero MoveDir (the only way InitialUpdate makes NaNs):
		// a door that does not move.
		vMoveDir.Init(0.0f, 0.0f, 0.0f);
		bRepaired = LTTRUE;
	}

	if (P5_IsNaN(vOpenPos) && !P5_IsNaN(vClosedPos))
	{
		vOpenPos = vClosedPos + vMoveDir * fMoveDist;
		bRepaired = LTTRUE;
	}

	return bRepaired;
}
