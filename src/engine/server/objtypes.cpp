// Basic native object construction and per-type setup.
#include <string.h>
#include "ltlink.h"
#include "objtypes.h"
#include "ltobject_internal.h"
#include "de_error.h"
#include "engine/portable/allocation.h"
#include <cmath>

#include "object_manager.h"

CObjWorldModel::CObjWorldModel() : CObjBase(OT_WORLDMODEL)
{
	InitFields();
}

void CObjWorldModel::InitFields()
{
	uint32 i;

	m_ColorB = 255;
	m_ColorG = 255;
	m_ColorR = 255;

	m_pWorldBsp = LTNULL;
	m_pOriginalBsp = LTNULL;
	m_pWMIface = LTNULL;

	m_Transform1f0.Identity();
	m_Transform230.Identity();

	for (i = 0; i < 5; i++)
		((LTLink*)m_Links1b4)[i].Init2(this);
}

CObjWorldModel::CObjWorldModel(char nType) : CObjBase(nType)
{
	InitFields();
}

CObjContainer::CObjContainer() : CObjWorldModel(OT_CONTAINER)
{
	m_ContainerCode = 0xffff;
}

CObjLight::CObjLight() : CObjBase(OT_LIGHT)
{
	m_fLightRadius = 100.0f;
}

float CObjLight::GetLightRadius(void* pUnused)
{
	return m_fLightRadius;
}

CObjCanvas::CObjCanvas() : CObjBase(OT_CANVAS)
{
}

float CObjCanvas::_v2c()
{
	return m_VisRadius;
}

float CObjCanvas::_v38()
{
	return m_VisRadius;
}

CObjCamera::CObjCamera() : CObjBase(OT_CAMERA)
{
	m_fFovY = m_fFovX = 1.5707964f;

	m_Unknown1c0 = 1;

	m_v1c4.Init();
}

CObjLineSystem::CObjLineSystem() : CObjBase(OT_LINESYSTEM)
{
	m_pCursorA = &m_Head;
	m_pCursorB = &m_Head;

	m_pUnknown1a8 = LTNULL;
	m_f218 = 0.0f;
	m_pUnknown1ac = LTNULL;

	m_v20c.Init();
	m_v1f4.Init();
	m_v200.Init();
}

int CUnknown390::Lock()
{
	LTLink* pLink;
	int     i;

	if (m_LockCount == -1)
	{
		m_LockCount = 1;

		for (i = 0; i < 11; i++)
		{
			for (pLink = m_Lists[i].m_Head.m_pNext;
			     pLink != &m_Lists[i].m_Head;
			     pLink = pLink->m_pNext)
			{
				((CObjBase*)pLink->m_pData)->m_Unknown58 = 0;
			}
		}
	}
	else
	{
		m_LockCount++;
	}

	return m_LockCount;
}

int CUnknown390::GetLockCount()
{
	return m_LockCount;
}

CObjWorldModel::~CObjWorldModel()
{
	sub_10024d41();
}

void CObjWorldModel::sub_10024d41()
{
	uint32 i;

	for (i = 0; i < 5; i++)
		((LTLink*)m_Links1b4)[i].Remove();

	CObjBase::_v08();
}

CObjLight::~CObjLight()
{
}

CObjCamera::~CObjCamera()
{
}

CObjContainer::~CObjContainer()
{
}

void CObjWorldModel::_v20(CUnknown390* pAttachMgr, ObjectCreateStruct* pStruct)
{
	CObjBase::_v20(pAttachMgr, pStruct);
}

void CObjContainer::_v20(CUnknown390* pAttachMgr, ObjectCreateStruct* pStruct)
{
	CObjWorldModel::_v20(pAttachMgr, pStruct);

	m_ContainerCode = pStruct->m_ContainerCode;
}

void CObjLineSystem::_v20(CUnknown390* pAttachMgr, ObjectCreateStruct* pStruct)
{
	CObjBase::_v20(pAttachMgr, pStruct);

	m_pUnknown1a8 = &pAttachMgr->m_Bank30;
}

void CObjWorldModel::LinkIntoTree(LTObject* pObject)
{
	obj_LinkIntoWorldTree((CWorldBspView*)m_pWorldBsp, pObject);
}

void CObjWorldModel::ForwardToWorldBsp()
{
	((IWorldBsp*)m_pWorldBsp)->IsFlagSet();
}

LTLink* CObjWorldModel::GetLink(uint32 iLink)
{
	return iLink < 5 ? m_Links1b4 + iLink : nullptr;
}

LTBOOL CObjWorldModel::HasOriginalBsp()
{
	return m_pOriginalBsp != LTNULL;
}

uint32 CObjWorldModel::GetWorldFlag4()
{
	return m_pWorldBsp ? m_pWorldBsp->GetWorldFlags() & WORLDFLAG_MAINWORLD : 0;
}

LTBOOL CObjWorldModel::IsWorldBusy()
{

	if (m_pWorldBsp)
		return ((IWorldBsp*)m_pWorldBsp)->IsBusy() == 1;

	return LTFALSE;
}

LTBOOL CObjWorldModel::IsWorldIdle()
{
	return ((IWorldBsp*)m_pWorldBsp)->IsBusy() == 0;
}

CObjLineSystem::~CObjLineSystem()
{
	CLineSegment* pSentinel = (CLineSegment*)&m_Head;
	CLineSegment* pCur = (CLineSegment*)m_pCursorB;

	while (pCur != pSentinel)
	{
		CLineSegment* pNext = pCur->m_pNext;

        auto* bank = static_cast<StructBank*>(m_pUnknown1a8);
        pCur->~CLineSegment();
        sb_Free(bank, pCur);

		pCur = pNext;
	}

	m_pCursorA = &m_Head;
	m_pCursorB = &m_Head;
	m_pUnknown1a8 = LTNULL;
}

LTRESULT om_CreateObject(CUnknown390* p, ObjectCreateStruct* pStruct,
                         CObjBase** ppObj)
{
	CObjBase* pObj;
	uint16    nType;

    if (!ppObj) return LT_INVALIDPARAMS;
	*ppObj = nullptr;
    if (!p || !pStruct) return LT_INVALIDPARAMS;

	nType = pStruct->m_ObjectType;
	if (nType >= NUM_OBJECTTYPES)
		return LT_INVALIDPARAMS;

    try { pObj = p->m_pBanks[nType]->Allocate(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
	if (!pObj)
		return LT_OUTOFMEMORY;

    try { p->m_Allocated.emplace(pObj,static_cast<uint8>(nType)); }
    catch (const std::bad_alloc&) { p->m_pBanks[nType]->Free(pObj); return LT_OUTOFMEMORY; }

	pObj->m_pAttachmentBank = &p->m_Bank4c;
	pObj->_v20(p, pStruct);

	dl_AddHead(&p->m_Lists[nType], &pObj->m_Link7c, pObj);

	*ppObj = pObj;
	return LT_OK;
}

CObjPolyGrid::CObjPolyGrid() : CObjBase(OT_POLYGRID)
{
	m_Records.TieOff();
}

float CObjPolyGrid::GetBoundingRadius()
{
	float fx = float(m_Width)*.5f * m_Scale.x;
	float fy = m_Scale.y * 128.0f;
	float fz = float(m_Height)*.5f * m_Scale.z;

	float fRadius = (float)sqrt(fz * fz + fy * fy + fx * fx);
	return fRadius + 1.0f;
}

CObjPolyGrid::~CObjPolyGrid()
{
    pg_FreeData(this);
}
