#include "world_polygon.h"
void math_GetAxisBasisVectors(uint32 iBasis, LTVector* pDir,
                              LTVector* pOut1, LTVector* pOut2);  // 0x1001457f

void CWorldSurface::GetBasisVectors(LTVector* pR, LTVector* pU)
{
	math_GetAxisBasisVectors(m_nAxis, &m_pPlane->m_Normal, pR, pU);
}



void w_SetupPolyTexCoordsQT1(CWorldPart* pPoly)
{
	WorldPolyVertex* pVert;
	WorldPolyVertex* pEnd;
	LTVector vDelta;

	pVert = pPoly->m_Vertices;
	pEnd  = pVert + pPoly->GetVertexCount();

	while (pVert != pEnd)
	{
		vDelta = *pVert->m_pPos - pPoly->m_pSurface->m_vO;

		pVert->m_fU = vDelta.Dot(pPoly->m_pSurface->m_vP);
		pVert->m_fV = vDelta.Dot(pPoly->m_pSurface->m_vQ);

		pVert++;
	}
}
