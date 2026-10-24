// Native object banks, model instances and manager lifetime.

#include <new>
#include <math.h>
#include "object_manager.h"
#include "engine/portable/allocation.h"
#include "objtypes.h"
#include "commonmodel.h"
#include <string.h>


void sb_FreeChunks(StructBank* pBank, int bAll);

void sb_Grow(StructBank* pBank, uint32 nCount);

using CObjNormal = LTObject;

ModelInstance::~ModelInstance() { *m_Lifetime = false; }

void LTAnimTracker::SetupTimeRef(AnimTimeRef* pOut)
{
	*pOut = m_TimeRef;
}

float ModelInstance::_v2c()
{
    const ModelRuntimeData* model = m_AnimTracker.m_TimeRef.m_pModel;
    return model ? model->m_f0a8 : 0;
}

float ModelInstance::GetScaledRadius()
{
	return LTMAX(m_Scale.x, LTMAX(m_Scale.y, m_Scale.z))
	       * _v2c();
}

void ModelInstance::SetupModelWalker(ModelWalker* pOut)
{
	LTAnimTracker* pTracker;

	pOut->m_Unknown_e0 = 0;

	for (pTracker = m_pTrackerList; pTracker;
	     pTracker = pTracker->GetNext())
	{
		if (pOut->m_Unknown_e0 >= 8)
			break;
		// A blending tracker without a weight set contributes nothing.
		if (pOut->m_Unknown_e0 && pTracker->m_TimeRef.m_iWeightSet == INVALID_MODEL_WEIGHTSET)
			continue;

		pTracker->SetupTimeRef(&pOut->m_TimeRefs[pOut->m_Unknown_e0]);
		pOut->m_Unknown_e0++;
	}

	pOut->m_Unknown_ec = m_Unknown1fc;
	pOut->m_Unknown_f0 = m_Unknown200;
    pOut->m_Unknown_f4 = this;
}

void ModelInstance::SetupModelWalkerRefsOnly(ModelWalker* pOut)
{
	LTAnimTracker* pTracker;

	pOut->m_Unknown_e0 = 0;

	for (pTracker = m_pTrackerList; pTracker;
	     pTracker = pTracker->GetNext())
	{
		if (pOut->m_Unknown_e0 >= 8)
			break;
		// A blending tracker without a weight set contributes nothing.
		if (pOut->m_Unknown_e0 && pTracker->m_TimeRef.m_iWeightSet == INVALID_MODEL_WEIGHTSET)
			continue;

		pTracker->SetupTimeRef(&pOut->m_TimeRefs[pOut->m_Unknown_e0]);
		pOut->m_Unknown_e0++;
	}
}

LTBOOL ModelInstance::FindTracker(LTAnimTracker* pTracker,
                                  LTAnimTracker*** pppLink)
{

	if (m_pTrackerList)
	{
		LTAnimTracker** ppCur = &m_pTrackerList;

		for (;;)
		{
			LTAnimTracker* pCur;

			*pppLink = ppCur;

			pCur = *ppCur;

			if (!pCur)
				break;
			if (pCur == pTracker)
				return LTTRUE;

			ppCur = (LTAnimTracker**)&pCur->m_Link.m_pNext;
		}
	}

	return LTFALSE;
}

void ModelInstance::DetachExternalTrackers()
{
    LTAnimTracker** link = &m_pTrackerList;
    while (auto* tracker = *link) {
        if (tracker == &m_AnimTracker) { link = reinterpret_cast<LTAnimTracker**>(&tracker->m_Link.m_pNext); continue; }
        *link = tracker->GetNext();
        ++tracker->m_MutationGeneration;
        tracker->m_Link.m_pData = nullptr;
        tracker->m_Link.m_pNext = nullptr;
    }
}

uint32 ModelInstance::GetTrackerCount()
{
	uint32 nCount = 0;
	LTAnimTracker* pTracker = m_pTrackerList;

	while (pTracker)
	{
		pTracker = pTracker->GetNext();
		nCount++;
	}

	return nCount;
}

char* ModelInstance::GetFilename()
{
	const ModelRuntimeData* pModel = m_AnimTracker.m_TimeRef.m_pModel;

	if (pModel)
		return pModel->m_pFilename;

	return "";
}

CObjSprite::~CObjSprite() {}

CObjSprite::CObjSprite() : CObjBase(OT_SPRITE)
{
	m_ColorB = 255;
	m_ColorG = 255;
	m_ColorR = 255;

	m_ClipPoly = INVALID_HPOLY;

	m_Control.m_pInstance = this;
}

ModelInstance::ModelInstance() : CObjBase(OT_MODEL)
{
    m_pUnknown1a8 = nullptr;
	m_pUnknown1ac = LTNULL;
	m_pUnknown1b0 = LTNULL;
	m_pUnknown1b8 = LTNULL;

	at_Init(&m_AnimTracker, LTNULL, LTFALSE);

	m_Unknown1fc   = 0;
	m_Unknown200   = 0;
	m_HiddenPieces = 0;

	m_HookFn = nullptr;

	m_pTrackerList = &m_AnimTracker;

	m_AnimTracker.m_Link.m_pNext = LTNULL;
	m_AnimTracker.m_Link.m_pData = this;

	m_pUnknown208 = m_pUnknown20c = nullptr;
	m_pUnknown1b8 = m_pUnknown1bc = nullptr;

	m_f244 = -1.0f;
}

CObjParticleSystem::CObjParticleSystem() : CObjBase(OT_PARTICLESYSTEM)
{
	m_u1e8 = 0;

	m_n254 = -1;
	m_n250 = -1;

	m_ColorB = 255;
	m_ColorG = 255;
	m_ColorR = 255;

	m_u1ee = 255;
	m_u1ed = 255;
	m_u1ec = 255;

	m_vBoxMin.z = 0.0f;
	m_vBoxMin.y = 0.0f;
	m_vBoxMin.x = 0.0f;

	m_vBoxMax.z = 0.0f;
	m_vBoxMax.y = 0.0f;
	m_vBoxMax.x = 0.0f;

	m_p1e0 = &m_Head;
	m_p1c8 = &m_Head;

	m_f210 = 0.0f;
	m_f20c = 0.0f;
	m_f208 = 0.0f;
	m_f214 = 1.0f;

	m_f24c = 300.0f;
	m_f248 = -500.0f;

	m_u258 = 0;
	m_p1e4 = LTNULL;
	m_u1f0 = 0;

	m_f220 = 0.0f;
	m_f21c = 0.0f;
	m_f218 = 0.0f;
	m_f224 = 1.0f;

	m_u228 = 0;
	m_u22c = 0;
}

CObjParticleSystem::~CObjParticleSystem()
{
	m_vBoxMin.Init(1.0e+37f, 1.0e+37f, 1.0e+37f);
	m_vBoxMax.Init(-1.0e+37f, -1.0e+37f, -1.0e+37f);

	m_u1e8 = 0;

	if (m_pAttachMgr)
	{
		CParticle* pSentinel = (CParticle*)&m_Head;
		CParticle* pCur = (CParticle*)m_p1c8;

		while (pCur != pSentinel)
		{
			CParticle* pNext = static_cast<CParticle*>(pCur->m_pNext);

			pCur->~CParticle();
			sb_Free(&m_pAttachMgr->m_Bank14, pCur);

			pCur = pNext;
		}
	}

	m_u228 = 0;
	m_p1e0 = &m_Head;
	m_p1c8 = &m_Head;
}

void CObjParticleSystem::_v20(CUnknown390* pAttachMgr,
                              ObjectCreateStruct* pStruct)
{
	CObjBase::_v20(pAttachMgr, pStruct);

	m_p1e4 = &pAttachMgr->m_Bank14;
}

float CObjSprite::GetScaledRadius()
{
	return LTMAX(m_Scale.x, m_Scale.y) * 363.0f;
}

template<class T>
class CObjectBank : public CObjectBankBase
{
public:

	CObjectBank(uint32 nGrowBy)
	{
		sb_InitAligned(&m_Bank, sizeof(T), 32, alignof(T));
        sb_AllocateNewStructPage(&m_Bank, nGrowBy);
	}

	virtual ~CObjectBank()
	{
		sb_FreeChunks(&m_Bank, 1);
	}

	LTObject* Allocate() override
	{
		T* pRet = (T*)sb_Allocate(&m_Bank);

		if (pRet) {
            try { new(pRet) T; }
            catch (...) { sb_Free(&m_Bank, pRet); throw; }
        }

		return pRet;
	}

	void Free(LTObject* pObj) override
	{
        auto* object = static_cast<T*>(pObj);
        object->~T();
        sb_Free(&m_Bank, object);
	}

	virtual void Init()
	{
		sb_FreeChunks(&m_Bank, 1);
		sb_InitAligned(&m_Bank, sizeof(T), 32, alignof(T));
	}

public:
	StructBank m_Bank;
	uint32     m_Unknown20;
};

CUnknown390::CUnknown390() {
    m_Link.Init2(this);
    m_Banks[OT_NORMAL] = std::make_unique<CObjectBank<CObjNormal>>(32);
    m_Banks[OT_MODEL] = std::make_unique<CObjectBank<CObjModel>>(256);
    m_Banks[OT_WORLDMODEL] = std::make_unique<CObjectBank<CObjWorldModel>>(32);
    m_Banks[OT_SPRITE] = std::make_unique<CObjectBank<CObjSprite>>(128);
    m_Banks[OT_LIGHT] = std::make_unique<CObjectBank<CObjLight>>(32);
    m_Banks[OT_CAMERA] = std::make_unique<CObjectBank<CObjCamera>>(32);
    m_Banks[OT_PARTICLESYSTEM] = std::make_unique<CObjectBank<CObjParticleSystem>>(32);
    m_Banks[OT_POLYGRID] = std::make_unique<CObjectBank<CObjPolyGrid>>(32);
    m_Banks[OT_LINESYSTEM] = std::make_unique<CObjectBank<CObjLineSystem>>(32);
    m_Banks[OT_CONTAINER] = std::make_unique<CObjectBank<CObjContainer>>(32);
    m_Banks[OT_CANVAS] = std::make_unique<CObjectBank<CObjCanvas>>(32);
    for (uint32 i = 0; i < NUM_OBJECTTYPES; ++i) m_pBanks[i] = m_Banks[i].get();
    un390_Init(this, 0);
}
CUnknown390::~CUnknown390() { un390_Term(this); }
int un390_Init(CUnknown390* manager, int dedicated) {
    un390_Term(manager);
    sb_InitAligned(&manager->m_Bank14, sizeof(CParticle), dedicated ? 1024 : 64, alignof(CParticle));
    sb_InitAligned(&manager->m_Bank30, sizeof(CLineSegment), 64, alignof(CLineSegment));
    sb_InitAligned(&manager->m_Bank4c, sizeof(Attachment), 32, alignof(Attachment));
    manager->m_LockCount = -1;
    return 0;
}
int un390_Term(CUnknown390* manager) {
    for (uint32 i = 0; i < NUM_OBJECTTYPES; ++i) {
        auto& list = manager->m_Lists[i];
        while (list.m_Head.m_pNext != &list.m_Head)
            un390_RemoveObject(manager, static_cast<LTObject*>(list.m_Head.m_pNext->m_pData));
        if (manager->m_pBanks[i]) manager->m_pBanks[i]->Init();
    }
    sb_Term(&manager->m_Bank14);
    sb_Term(&manager->m_Bank30);
    sb_Term(&manager->m_Bank4c);
    manager->m_Link.Remove();
    return 0;
}

LTRESULT un390_RemoveObject(CUnknown390* p, LTObject* pObject)
{
    if (!p || !pObject) return LT_INVALIDPARAMS;
    const auto found = p->m_Allocated.find(pObject);
    if (found == p->m_Allocated.end()) return LT_NOTFOUND;
    const auto type = found->second;
    *pObject->m_Lifetime = false;
    am_DetachObject(p,pObject);
    p->m_Allocated.erase(found);
    dl_RemoveAt(&p->m_Lists[type], &pObject->m_Link7c);
    p->m_pBanks[type]->Free(pObject);
    return LT_OK;
}
