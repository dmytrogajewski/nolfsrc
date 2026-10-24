// Server SDK ownership and notification adapters for the shared physics algorithms.
#include "commonphysics.h"
#include "servermgr.h"
#include "servermsg.h"
#include "servercommon.h"
#include "servermodel.h"
#include "serverlightanim.h"
#include "ilttransform.h"
#include "de_error.h"
#include <cmath>

void si_SetupInterface(CLTServer*);

inline SPhysicsLT::SPhysicsLT(CServerMgr* pServerMgr)
	: m_StairHeight(-1.0f)
{
	m_pWorld           = pServerMgr;
	m_ClientServerType = ServerType;
}

CLTServer::CLTServer(CServerMgr* pServerMgr)
	: m_ModelLT(pServerMgr), m_PhysicsLT(pServerMgr), m_LightAnimLT(pServerMgr)
{
	m_pServerMgr   = pServerMgr;

	m_pCommonLT    = &m_CommonLT;
	m_pTransformLT = &m_TransformLT;
	m_pModelLT     = &m_ModelLT;
	m_pPhysicsLT   = &m_PhysicsLT;
	m_pLightAnimLT = &m_LightAnimLT;

	m_pSoundMgr = static_cast<ILTSoundMgr*>(pServerMgr);

	m_CommonLT.SetMath(&m_MathLT);
	m_CommonLT.m_pServerMgr   = pServerMgr;
	m_CommonLT.m_pModelLT     = &m_ModelLT;
	m_CommonLT.m_pTransformLT = &m_TransformLT;
}

CUnknown928Obj* sub_1002d355(CServerMgr* pMgr)
{
	CLTServer* pServer = new CLTServer(pMgr);

	if (pServer)
		si_SetupInterface(pServer);

	return (CUnknown928Obj*)pServer;
}

LTRESULT SPhysicsLT::SetVelocity(HOBJECT hObj, LTVector* pVel)
{
	if (!hObj || !pVel || !m_pWorld->m_Unknown390.m_Allocated.contains(hObj))
		RETURN_ERROR(1, "ILTPhysics::SetVelocity", LT_INVALIDPARAMS);

	if (hObj->m_Velocity.DistSqr(*pVel) < 0.001f)
		return LT_OK;

	hObj->m_ChangeFlags |= CHANGEFLAG_MOTION;
	hObj->m_Velocity = *pVel;

	return LT_OK;
}

LTRESULT SPhysicsLT::SetAcceleration(HOBJECT hObj, LTVector* pAccel)
{
	if (!hObj || !pAccel || !m_pWorld->m_Unknown390.m_Allocated.contains(hObj))
		RETURN_ERROR(1, "ILTPhysics::SetAcceleration", LT_INVALIDPARAMS);

	if (hObj->m_Acceleration.DistSqr(*pAccel) < 0.001f)
		return LT_OK;

	hObj->m_ChangeFlags |= CHANGEFLAG_MOTION;
	hObj->m_Acceleration = *pAccel;

	return LT_OK;
}

LTRESULT SPhysicsLT::MoveObject(HOBJECT hObj, LTVector* pPos, uint32 flags)
{
	if (!hObj || !pPos || !m_pWorld->m_Unknown390.m_Allocated.contains(hObj))
		RETURN_ERROR(1, "ILTPhysics::MoveObject", LT_INVALIDPARAMS);

	sm_MoveObject(m_pWorld, hObj, pPos, 7);
	return LT_OK;
}

LTRESULT SPhysicsLT::GetGlobalForce(LTVector& vec)
{
	vec = m_pWorld->m_ForceControl.m_Force;
	return LT_OK;
}

LTRESULT SPhysicsLT::SetGlobalForce(LTVector& vec)
{
	m_pWorld->m_ForceControl.Set(&vec);
	return LT_OK;
}

LTRESULT SPhysicsLT::GetStairHeight(float& fHeight)
{
	fHeight = m_StairHeight;
	return LT_OK;
}

LTRESULT SPhysicsLT::SetStairHeight(float fHeight)
{
	m_StairHeight = fHeight;
	return LT_OK;
}

LTRESULT SPhysicsLT::SetObjectDims(HOBJECT hObj, LTVector* pNewDims, uint32 flags)
{
	WMMoveState state;
	LTVector vDims;

	state.m_pContainer = LTNULL;
	state.m_pUnknown04 = LTNULL;
	state.m_pObj = LTNULL;
	state.m_nUnknown64 = 0;
	if (!hObj || !pNewDims || !m_pWorld->m_Unknown390.m_Allocated.contains(hObj))
		RETURN_ERROR(2, "SPhysicsLT::SetObjectDims", LT_INVALIDPARAMS);
    vDims = *pNewDims;
    for (uint32 axis = 0; axis < 3; ++axis) if (!std::isfinite(vDims[axis]) || vDims[axis] < 0) return LT_INVALIDPARAMS;

	if (pNewDims->x > g_DebugMaxDims || pNewDims->y > g_DebugMaxDims ||
	    pNewDims->z > g_DebugMaxDims)
	{
		dsi_ServerError("ILTPhysics::SetObjectDims: dims larger than 'DebugMaxDims'");
		dsi_ServerError("Object class: %s, dims (%.2f, %.2f, %.2f)",
			hObj->m_pSData->m_pClass->m_ClassName,
			pNewDims->x, pNewDims->y, pNewDims->z);
	}

	if (hObj->m_ObjectType == OT_CONTAINER || hObj->m_ObjectType == OT_WORLDMODEL)
		return LT_INVALIDPARAMS;

	CServerMgr* pMgr = (CServerMgr*)m_pWorld;
	state.Init(&pMgr->m_Unknown210, pMgr->m_pUnknown6a8, hObj,
	                 hObj->m_BlockingPriority);

	if (wm_SetObjectDims(&state, &vDims, flags & 1, 1))
		return LT_OK;

	// Refused: hand back the dims the simulation would have accepted.
	*pNewDims = vDims;
	return LT_ERROR;
}

void sm_MoveObject(CServerMgr* pWorld, HOBJECT hObj, LTVector* pPos, uint32 flags)
{
    if (!pWorld || !pPos || !pWorld->m_Unknown390.m_Allocated.contains(hObj)) return;
    for (uint32 axis = 0; axis < 3; ++axis) if (!std::isfinite((*pPos)[axis])) return;
	g_Unknown58728++;

	// PROBE: the shipped client's copy of this function (lithtech 0x0047c5b0)
	// has no CountAdder at all -- no ctor, no cnt_StartCounter, no dtor, and a
	// frame exactly 12 bytes smaller (the counter's 8 plus m_pTotal). The
	// server's (0x1001fb22) has all three. Same shared TU, so a build-level
	// selector, not a sibling file.
#ifdef LT_SERVER_TIMERS
	CountAdder  cntAdder((uint32*)&g_Unknown5872c);
#endif
	WMMoveState state;
	CServerMgr* pMgr = (CServerMgr*)pWorld;

	state.m_nUnknown64 = 0;
	state.m_pContainer = &pMgr->m_Unknown210;
	state.m_pUnknown04 = pMgr->m_pUnknown6a8;
	state.m_pObj = hObj;
	state.m_nUnknown0C = hObj->m_BlockingPriority;
	state.m_pPushList  = LTNULL;
	state.m_nPushCount = 0;

	wm_MoveObject(&state, *pPos, flags);
}
