// *********************************************************************** //
//
//  MODULE:   CLIENTSOUND (.CPP)
//
//  PURPOSE:  The client's ILTSoundMgr.
//
// *********************************************************************** //

#include <string.h>

#include "clientsound.h"
#include "de_error.h"


LTRESULT CSoundMgr::InitSound(InitSoundInfo* pSoundInfo)
{
	if (!pSoundInfo)
		RETURN_ERROR(1, "CSoundMgr::InitSound", LT_INVALIDPARAMS);

	return Init(pSoundInfo);
}

// With no sound system there is no duration to report, and that is not an
// error: the answer is zero. The length itself is milliseconds, converted
// as unsigned and scaled to seconds.
LTRESULT CSoundMgr::GetSoundDuration(HLTSOUND hSound, LTFLOAT& fDuration)
{
	CSoundInstance* pSound;

	if (!m_pSoundSys)
	{
		fDuration = 0.0f;
		return LT_OK;
	}

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundDuration", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundDuration", LT_INVALIDPARAMS);

	{
		CSoundData* pData = pSound->m_pData;

		if (!pData)
			RETURN_ERROR(1, "CSoundMgr::GetSoundDuration", LT_INVALIDPARAMS);

		fDuration = pData->m_nDurationMS * 0.001f;
	}

	return LT_OK;
}

LTRESULT CSoundMgr::SetSoundOcclusion(HLTSOUND hSound, LTFLOAT fLevel)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundOcclusion", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundOcclusion", LT_ERROR);

	pSound->SetOcclusion(fLevel);
	return LT_OK;
}

// A null destination is not refused, just not written to.
LTRESULT CSoundMgr::GetSoundOcclusion(HLTSOUND hSound, LTFLOAT* pLevel)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundOcclusion", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundOcclusion", LT_ERROR);

	if (pLevel)
		pSound->GetOcclusion(pLevel);

	return LT_OK;
}

LTRESULT CSoundMgr::SetSoundObstruction(HLTSOUND hSound, LTFLOAT fLevel)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundObstruction", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundObstruction", LT_ERROR);

	pSound->SetObstruction(fLevel);
	return LT_OK;
}

LTRESULT CSoundMgr::GetSoundObstruction(HLTSOUND hSound, LTFLOAT* pLevel)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundObstruction", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundObstruction", LT_ERROR);

	if (pLevel)
		pSound->GetObstruction(pLevel);

	return LT_OK;
}

LTRESULT CSoundMgr::GetSoundPosition(HLTSOUND hSound, LTVector* pPos)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundPosition", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundPosition", LT_ERROR);

	if (pPos)
		pSound->GetPosition(pPos);

	return LT_OK;
}

LTRESULT CSoundMgr::SetSoundPosition(HLTSOUND hSound, LTVector* pPos)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundPosition", LT_INVALIDPARAMS);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundPosition", LT_ERROR);

	if (pPos)
		pSound->SetPosition(pPos, 0);

	return LT_OK;
}

// Only the fields the caller marked are written, and the last two are both
// marked by the decay-time bit: the damping test was copied from the one
// above it and the constant never changed, so a caller who asks for damping
// alone gets nothing back.
LTRESULT CSoundMgr::GetReverbProperties(ReverbProperties* pReverbProperties)
{
	if (!pReverbProperties)
		RETURN_ERROR(1, "CSoundMgr::GetReverbProperties", LT_INVALIDPARAMS);

	if (m_pSoundSys && (m_bReverbA || m_bReverbB))
	{
		if (pReverbProperties->m_dwParams & REVERBPARAM_ACOUSTICS)
			pReverbProperties->m_dwAcoustics = m_dwAcoustics;

		if (pReverbProperties->m_dwParams & REVERBPARAM_VOLUME)
			pReverbProperties->m_fVolume = m_fVolume;

		if (pReverbProperties->m_dwParams & REVERBPARAM_REFLECTTIME)
			pReverbProperties->m_fReflectTime = m_fReflectTime;

		if (pReverbProperties->m_dwParams & REVERBPARAM_DECAYTIME)
			pReverbProperties->m_fDecayTime = m_fDecayTime;

		// fixed: damping has its own flag, as SetReverbProperties uses (RETAIL_BUGS.md)
		if (pReverbProperties->m_dwParams & REVERBPARAM_DAMPING)
			pReverbProperties->m_fDamping = m_fDamping;

		return LT_OK;
	}

	return LT_ERROR;
}

// Each marked field is clamped to the range iltbasedefs.h documents for it,
// and then compared against the copy the manager keeps: the room type, the
// volume, the decay and the damping all note a change, and only the
// reflection time is written through without one. The clamped value is
// written back into the caller's struct for every field except the
// reflection time, which the manager keeps for itself.
//
// A change is what makes the call worth anything: with nothing changed the
// Miles room type is left alone.
LTRESULT CSoundMgr::SetReverbProperties(ReverbProperties* pReverbProperties)
{
	if (!pReverbProperties)
		RETURN_ERROR(1, "CSoundMgr::SetReverbProperties", LT_INVALIDPARAMS);

	if (m_pSoundSys && (m_bReverbA || m_bReverbB))
	{
		LTBOOL bChanged = LTFALSE;

		if (pReverbProperties->m_dwParams & REVERBPARAM_ACOUSTICS)
		{
			if (pReverbProperties->m_dwAcoustics >= REVERB_ACOUSTICS_COUNT)
				pReverbProperties->m_dwAcoustics = 0;

			if (m_dwAcoustics != pReverbProperties->m_dwAcoustics)
			{
				bChanged = LTTRUE;
				m_dwAcoustics = pReverbProperties->m_dwAcoustics;
			}
		}

		if (pReverbProperties->m_dwParams & REVERBPARAM_VOLUME)
		{
			pReverbProperties->m_fVolume =
			    pReverbProperties->m_fVolume < 0.0f ? 0.0f
			    : (pReverbProperties->m_fVolume > 1.0f ? 1.0f
			                                           : pReverbProperties->m_fVolume);

			if (pReverbProperties->m_fVolume != m_fVolume)
			{
				m_fVolume = pReverbProperties->m_fVolume;
				bChanged = LTTRUE;
			}
		}

		if (pReverbProperties->m_dwParams & REVERBPARAM_REFLECTTIME)
		{
			m_fReflectTime =
			    pReverbProperties->m_fReflectTime < 0.0f ? 0.0f
			    : (pReverbProperties->m_fReflectTime > 5.0f ? 5.0f
			                                                : pReverbProperties->m_fReflectTime);
		}

		if (pReverbProperties->m_dwParams & REVERBPARAM_DECAYTIME)
		{
			pReverbProperties->m_fDecayTime =
			    pReverbProperties->m_fDecayTime < 0.001f ? 0.001f
			    : (pReverbProperties->m_fDecayTime > 20.0f ? 20.0f
			                                               : pReverbProperties->m_fDecayTime);

			if (pReverbProperties->m_fDecayTime != m_fDecayTime)
			{
				m_fDecayTime = pReverbProperties->m_fDecayTime;
				bChanged = LTTRUE;
			}
		}

		if (pReverbProperties->m_dwParams & REVERBPARAM_DAMPING)
		{
			pReverbProperties->m_fDamping =
			    pReverbProperties->m_fDamping < 0.0f ? 0.0f
			    : (pReverbProperties->m_fDamping > 2.0f ? 2.0f
			                                            : pReverbProperties->m_fDamping);

			if (pReverbProperties->m_fDamping != m_fDamping)
			{
				m_fDamping = pReverbProperties->m_fDamping;
				bChanged = LTTRUE;
			}
		}

		if (m_bReverbB && bChanged)
		{
			AIL_lock();
			AIL_set_3D_room_type(m_h3DProvider, m_dwAcoustics);
			AIL_unlock();
		}

		return LT_OK;
	}

	return LT_ERROR;
}

// The filter calls look the sound up twice: once the ordinary way and once
// the other way, so a sound that has stopped can still be filtered. Their
// codes are the other way round from the rest of the file too - a null handle
// is LT_ERROR and a sound that cannot be found is LT_INVALIDPARAMS.
LTRESULT CSoundMgr::SetSoundFilter(HLTSOUND hSound, const char* pFilter)
{
	CSoundInstance* pSound;

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundFilter", LT_ERROR);

	pSound = GetSoundInstance(hSound, 0);

	if (!pSound)
	{
		pSound = GetSoundInstance(hSound, 1);

		if (!pSound)
			RETURN_ERROR(1, "CSoundMgr::SetSoundFilter", LT_INVALIDPARAMS);
	}

	if (!pSound->SetFilter(pFilter))
		RETURN_ERROR(1, "CSoundMgr::SetSoundFilter", LT_ERROR);

	return LT_OK;
}

LTRESULT CSoundMgr::SetSoundFilterParam(HLTSOUND hSound, const char* pParam, float fValue)
{
	CSoundInstance* pSound;

	if (!pParam)
		RETURN_ERROR(1, "CSoundMgr::SetSoundFilterParam", LT_INVALIDPARAMS);

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::SetSoundFilterParam", LT_ERROR);

	pSound = GetSoundInstance(hSound, 0);

	if (!pSound)
	{
		pSound = GetSoundInstance(hSound, 1);

		if (!pSound)
			RETURN_ERROR(1, "CSoundMgr::SetSoundFilterParam", LT_INVALIDPARAMS);
	}

	if (!pSound->SetFilterParam(pParam, fValue))
		RETURN_ERROR(1, "CSoundMgr::SetSoundFilterParam", LT_NOTFOUND);

	return LT_OK;
}

// Reading looks the sound up only one way, and refuses a sound that has no
// filter on it at all.
LTRESULT CSoundMgr::GetSoundFilterParam(HLTSOUND hSound, const char* pParam, float* pValue)
{
	CSoundInstance* pSound;

	if (!pParam || !pValue)
		RETURN_ERROR(1, "CSoundMgr::GetSoundFilterParam", LT_INVALIDPARAMS);

	if (!hSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundFilterParam", LT_ERROR);

	pSound = GetSoundInstance(hSound, 1);

	if (!pSound)
		RETURN_ERROR(1, "CSoundMgr::GetSoundFilterParam", LT_INVALIDPARAMS);

	if (!pSound->m_pFilter)
		RETURN_ERROR(1, "CSoundMgr::GetSoundFilterParam", LT_INVALIDPARAMS);

	if (!pSound->GetFilterParam(pParam, pValue))
		RETURN_ERROR(1, "CSoundMgr::GetSoundFilterParam", LT_NOTFOUND);

	return LT_OK;
}

// Miles enumerates its filters one at a time, so the n'th is reached by
// asking n+1 times, counting the index parameter itself down. The
// enumeration cursor is a local that VC6 puts in the dead argument slot,
// which is why the function reserves only two words for the two things Miles
// hands back. The walk is one plain while over the call: VC6 rotates it
// itself, peeling the first call and leaving the count test at the loop top
// with a `jmp` out after the back edge, as shipped. Rotating it by hand
// (a leading call and a for(;;)) put a second count test at the bottom.
LTRESULT CSoundMgr::GetFilterName(uint32 nIndex, const char** pFilter)
{
	char*  pName;
	void*  hFilter;
	uint32 nNext;

	if (!pFilter)
		RETURN_ERROR(1, "CSoundMgr::GetFilterName", LT_INVALIDPARAMS);

	*pFilter = LTNULL;
	nNext = 0;

	while (AIL_enumerate_filters(&nNext, &hFilter, &pName))
	{
		if (!nIndex)
		{
			*pFilter = pName;
			break;
		}

		nIndex--;
	}

	if (!*pFilter)
		RETURN_ERROR(1, "CSoundMgr::GetFilterName", LT_NOTFOUND);

	return LT_OK;
}

// Two walks, one after the other: find the filter whose name was asked for,
// then step its parameters to the index. The filter walk clears the handle
// on every iteration it does not match on, so running the list out leaves it
// null and the not-found test below has something to see.
//
// The parameter walk is written the same way as GetFilterName's: one plain
// while over the call, counting nIndex down, and VC6 does the rotation.
LTRESULT CSoundMgr::GetFilterParamName(uint32 nIndex, const char* pFilter,
                                       const char** pParam)
{
	RIBEntry entry;
	uint32   nAttrNext;
	char*    pName;
	void*    hFilter;
	uint32   nNext;

	if (!pFilter || !pParam)
		RETURN_ERROR(1, "CSoundMgr::GetFilterParamName", LT_INVALIDPARAMS);

	*pParam = LTNULL;
	nNext = 0;

	while (AIL_enumerate_filters(&nNext, &hFilter, &pName))
	{
		if (!stricmp(pFilter, pName))
			break;

		hFilter = LTNULL;
	}

	if (!hFilter)
		RETURN_ERROR(1, "CSoundMgr::GetFilterParamName", LT_NOTFOUND);

	nAttrNext = 0;

	while (AIL_enumerate_filter_sample_attributes(hFilter, &nAttrNext, &entry))
	{
		if (!nIndex)
		{
			*pParam = entry.m_pName;
			break;
		}

		nIndex--;
	}

	if (!*pParam)
		RETURN_ERROR(1, "CSoundMgr::GetFilterParamName", LT_NOTFOUND);

	return LT_OK;
}

// The inverse of GetFilterParamName, and unlike GetFilterIndex below it is
// the right way round: the walk stops on the name that matches, and the index
// it answers is how many parameters it stepped over to get there. Only
// running the parameters out reports not-found, and that path zeroes the
// index it had been counting into -- which the missing-filter path above it
// does not, because it never started counting.
//
// NEAR, 345 bytes against 346, and the residual is register allocation
// only: the instruction streams match one for one, but the parameter walk
// takes ecx/edx the other way round in its argument setup and its stricmp
// loads, and the second error tail loads g_pErrorFormat into eax (the
// 5-byte moffs form) where shipped uses edx -- the one byte. ++*pIndex,
// an explicit else, a for-header walk, a named pAttr local and the reverse
// local declaration order each left it exactly as it is.
LTRESULT CSoundMgr::GetFilterParamIndex(const char* pFilter, const char* pParam,
                                        uint32* pIndex)
{
	RIBEntry entry;
	uint32   nAttrNext;
	char*    pName;
	void*    hFilter;
	uint32   nNext;

	if (!pFilter || !pParam || !pIndex)
		RETURN_ERROR(1, "CSoundMgr::GetFilterParamIndex", LT_INVALIDPARAMS);

	*pIndex = 0;
	nNext = 0;

	while (AIL_enumerate_filters(&nNext, &hFilter, &pName))
	{
		if (!stricmp(pFilter, pName))
			break;

		hFilter = LTNULL;
	}

	if (hFilter)
	{
		nAttrNext = 0;

		while (AIL_enumerate_filter_sample_attributes(hFilter, &nAttrNext, &entry))
		{
			if (!stricmp(entry.m_pName, pParam))
				return LT_OK;

			(*pIndex)++;
		}

		*pIndex = 0;
	}

	RETURN_ERROR(1, "CSoundMgr::GetFilterParamIndex", LT_NOTFOUND);
}

// The inverse of GetFilterName, and in the retail build it does not work: the
// comparison is the wrong way round. A filter whose name differs from the one
// asked for is reported as found, at whatever index the walk had reached, and
// a name that actually matches makes the walk carry on past it. Only running
// out of filters answers LT_NOTFOUND - which, with this test, is what happens
// when every filter matches. NOLFSRC_MODERN fixes it (RETAIL_BUGS.md).
LTRESULT CSoundMgr::GetFilterIndex(const char* pFilter, uint32* pIndex)
{
	void*  hFilter;
	char*  pName;
	uint32 nNext;

	if (!pFilter || !pIndex)
		RETURN_ERROR(1, "CSoundMgr::GetFilterIndex", LT_INVALIDPARAMS);

	*pIndex = 0;
	nNext = 0;

	while (AIL_enumerate_filters(&nNext, &hFilter, &pName))
	{
		if (stricmp(pFilter, pName) == 0)       // fixed: report the filter that matches
			return LT_OK;

		(*pIndex)++;
	}

	*pIndex = 0;
	RETURN_ERROR(1, "CSoundMgr::GetFilterIndex", LT_NOTFOUND);
}
