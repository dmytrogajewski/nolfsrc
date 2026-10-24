#include "commonmodel.h"

void at_Init(LTAnimTracker* pTracker, Model* pModel, uint32 iAnim)
{
	if (!pTracker) return;
    ++pTracker->m_MutationGeneration;
    pTracker->m_InterpolationElapsed = 0;
	uint16 nFlags;
    const ModelRuntimeData* model_data = pModel;

	pTracker->m_StringKeyCallback = LTNULL;
	pTracker->m_pUser1 = LTNULL;
	pTracker->m_TimeRef.m_pModel = pModel;
	pTracker->m_CurKey = 0;
	pTracker->m_Flags = 0;
	pTracker->m_InterpolationMS = 0;

	pTracker->m_TimeRef.m_Prev.Clear();
	pTracker->m_TimeRef.m_Cur.Clear();
	pTracker->m_TimeRef.m_Percent = 0.0f;

	if (pModel)
	{
		nFlags = pTracker->m_Flags;

		if (!(nFlags & 8) && iAnim >= model_data->m_AnimArray.m_nElements)
			iAnim = 0;

		pTracker->m_TimeRef.m_Cur.m_iAnim = (uint16)iAnim;
		pTracker->m_Flags = (uint16)(nFlags | 1);
		pTracker->m_TimeRef.m_Prev.m_iAnim = (uint16)iAnim;
		pTracker->m_TimeRef.m_Cur.m_iFrame = 0;
		pTracker->m_TimeRef.m_Prev.m_iFrame = 0;
		pTracker->m_InterpolationMS = 0xffff;
	}

	pTracker->m_Flags |= 2;
}
