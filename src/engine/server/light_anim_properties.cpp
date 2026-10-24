#include "light_anim_properties.h"

// ---- the record and the caller's view ------------------------------------ //

// 0x100129da — everything a light animation can be asked about. The shadow-map
// flag comes out with the rest even though nothing can put it back: it was
// fixed when the animation was baked.
void la_GetInfo(LightAnim* pAnim, LAInfo* pInfo)
{
	pInfo->m_bShadowMap = pAnim->m_bShadowMap;
	pInfo->m_iFrames[0] = pAnim->m_iFrames[0];
	pInfo->m_iFrames[1] = pAnim->m_iFrames[1];
	pInfo->m_fPercentBetween = pAnim->m_fPercentBetween;
	pInfo->m_fBlendPercent = pAnim->m_fBlendPercent;
	pInfo->m_vLightPos = pAnim->m_vLightPos;
	pInfo->m_vLightColor = pAnim->m_vLightColor;
	pInfo->m_fLightRadius = pAnim->m_fLightRadius;
}

// 0x10012ac3 — what about an animation the caller's LAInfo would change, as a
// bit per field, and whether that is anything at all.
//
// Nothing is compared exactly: every float goes through a quantization first,
// so a change too small for the wire format to carry is not a change. The
// frame pair is the exception — those are integers and compare directly.
//
// Read off the bytes rather than guessed:
//
//   * the quantizer is `(int)(x * 32.0f)` for the two percentages and a bare
//     `(int)x` for the radius. The 32.0f is loaded once into %st(1) and four
//     `fmul %st(1),%st` share it, which is VC6 commoning the constant across
//     four separate `* 32.0f` in the source, not one hoisted local.
//   * the position and colour go through 0x10012a59 with scales of 10.0 and
//     255.0 — the .rdata operands are doubles, and 255.0 is the range the SDK
//     header documents for the colour.
//   * the first flag is `movl $0x1,(%eax)` where the other five are `orl`.
//     That is VC6 folding `|= 1` into a store because the zeroing three
//     instructions earlier is still the last thing to have touched the word;
//     the later ones reload the pointer because __ftol clobbers eax.
//   * the position, colour and radius are only looked at for a shadow-mapped
//     animation (`cmpl $0x0,0x20(%esi)` jumps straight to the return),
//     matching la_SetInfo, which cannot change the flag itself.
LTBOOL la_CompareInfo(LightAnim* pAnim, LAInfo* pInfo, uint32* pChangeFlags)
{
	*pChangeFlags = 0;

	if (pAnim->m_iFrames[0] != pInfo->m_iFrames[0] ||
	    pAnim->m_iFrames[1] != pInfo->m_iFrames[1])
	{
		*pChangeFlags |= 1;
	}

	if ((int)(pAnim->m_fPercentBetween * 32.0f) !=
	    (int)(pInfo->m_fPercentBetween * 32.0f))
	{
		*pChangeFlags |= 2;
	}

	if ((int)(pAnim->m_fBlendPercent * 32.0f) !=
	    (int)(pInfo->m_fBlendPercent * 32.0f))
	{
		*pChangeFlags |= 4;
	}

	if (pAnim->m_bShadowMap)
	{
		if (la_QuantizedVecDiffers(&pAnim->m_vLightPos, &pInfo->m_vLightPos, 10.0f))
			*pChangeFlags |= 8;

		if (la_QuantizedVecDiffers(&pAnim->m_vLightColor, &pInfo->m_vLightColor, 255.0f))
			*pChangeFlags |= 0x10;

		if ((int)pAnim->m_fLightRadius != (int)pInfo->m_fLightRadius)
			*pChangeFlags |= 0x20;
	}

	return *pChangeFlags != 0;
}

// 0x10012a1c — and back the other way, without the shadow-map flag.
void la_SetInfo(LightAnim* pAnim, LAInfo* pInfo)
{
	pAnim->m_iFrames[0] = pInfo->m_iFrames[0];
	pAnim->m_iFrames[1] = pInfo->m_iFrames[1];
	pAnim->m_fPercentBetween = pInfo->m_fPercentBetween;
	pAnim->m_fBlendPercent = pInfo->m_fBlendPercent;
	pAnim->m_vLightPos = pInfo->m_vLightPos;
	pAnim->m_vLightColor = pInfo->m_vLightColor;
	pAnim->m_fLightRadius = pInfo->m_fLightRadius;
}

// 0x10012a59 — the position/colour half of the comparison above: quantize
// both vectors by the same scale and answer whether any component lands on a
// different integer.
//
// The scale is a double, not a float: `fmull 0x10(%ebp)` reads eight bytes
// and the caller pushes eight, so `pA->x * fScale` widens the component
// rather than narrowing the scale.
//
// One shared `push $1; pop %eax` serves all three failures, which is the
// OR-merged condition and not three separate returns.
int la_QuantizedVecDiffers(LTVector* pA, LTVector* pB, double fScale)
{
	if ((int)(pA->x * fScale) != (int)(pB->x * fScale) ||
	    (int)(pA->y * fScale) != (int)(pB->y * fScale) ||
	    (int)(pA->z * fScale) != (int)(pB->z * fScale))
	{
		return 1;
	}

	return 0;
}


