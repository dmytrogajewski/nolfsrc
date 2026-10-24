#include "worldbsp.h"
#include "world_record.h"
#include "engine/portable/allocation.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

uint32 g_WorldBytes = 0;

void CWorldBsp::Init()
{
    ++m_GeometryLifetime->generation;
    m_GeometryLifetime->available = m_GeometryLifetime->alive;
    m_Visibility.reset();
	m_Flags4c = 0;
	m_nBytes  = 0;

	m_p68 = LTNULL;
	m_n6c = 0;
	m_n70 = 0;
	m_n74 = 0;
	m_pTree = LTNULL;
	m_nSurfaces = 0;
	m_p80 = LTNULL;
	m_n84 = 0;
	m_p88 = LTNULL;
	m_n8c = 0;
	m_p90 = LTNULL;
	m_n94 = 0;
	m_n98 = 0;
	m_n9c = 0;
	m_ppParts = LTNULL;
	m_nParts = 0;
	m_pa8 = LTNULL;
	m_nac = 0;
	m_pChunks = LTNULL;
	m_nChunks = 0;
	m_pb8 = LTNULL;
	m_pbc = LTNULL;
	m_nc0 = 0;

	m_vBoxMin.Init();
	m_vBoxMax.Init();

	m_ndc = 0;
	m_ne0 = 0;
	m_ne4 = 0;

	m_ve8.Init();

	m_pf4 = LTNULL;
	m_nf8 = 0;
	m_pfc = LTNULL;
	m_n100 = 0;
	m_p104 = LTNULL;

    m_nUnknown48 = 0;
    m_fDiagonal = 0;
    m_Name[0] = 0;
    m_SurfaceProperties.clear();
}

void CWorldBsp::Term()
{
    if (m_Terminating) return;
    m_Terminating = true;
    m_GeometryLifetime->available = false;
    ++m_GeometryLifetime->generation;
    m_Visibility.reset();
    uint32 i;
    // Notify while every surface, plane and terrain section is still live.
    if (!(m_Flags4c & 1) && m_pTree)
        for (i = 0; i < m_nSurfaces; ++i) m_pTree[i].m_Sub28.sub_10024419();

    m_Array54.Term(&g_DefAlloc);
    m_Array138.Term(&g_DefAlloc);
    m_Sub108.Term();
    if (!(m_Flags4c & 1))
    {

		for (i = 0; m_pChunks && i < m_nChunks; i++)
		{
			if (m_pChunks[i].m_pName)
				dfree(m_pChunks[i].m_pName);
		}

		if (m_pChunks)
			dfree(m_pChunks);

		if (m_p80)
			dfree(m_p80);

		if (m_pfc)
			dfree(m_pfc);

		if (m_p104)
			dfree(m_p104);

		if (m_p88)
			delete[] m_p88;

		if (m_p90)
			dfree(m_p90);

		if (m_ppParts)
			dfree(m_ppParts);

		if (m_pa8)
			dfree(m_pa8);

		if (m_p68)
			dfree(m_p68);

		// 0x1000796d is `??_ECWorldTree` — VC6's vector deleting destructor —
		// and the three the shipped code pushes is its flag word: array plus
		// free. So this is one `delete []`, and the null test the shipped code
		// has around it is the compiler's own, not a guard in the source.
		delete [] m_pTree;

		if (m_pbc)
			dfree(m_pbc);

		if (m_pb8)
			dfree(m_pb8);

		if (m_pf4)
			dfree(m_pf4);

		g_WorldBytes -= m_nBytes;
	}

    FreeTree();
    Init();
    m_GeometryLifetime->available = false;
    m_Terminating = false;
}

CWorldBsp::CWorldBsp()
{
	Init();
}

CWorldBsp::~CWorldBsp()
{
	m_GeometryLifetime->alive = false;
	Term();
}

void CWorldBsp::FreeTree()
{
    m_GeometryLifetime->available = false;
    ++m_GeometryLifetime->generation;
    m_Visibility.reset();
	if (m_n70)
	{
		delete[] m_n70;

		m_n70 = 0;
	}
}

void CWorldBsp::Recalc()
{
	m_fDiagonal = wb_MaxDistSqr(m_ppParts, m_nParts, &m_ve8);
}

uint32 CWorldBsp::GetWorldFlags()
{
	return m_nUnknown48;
}

uint32 CWorldBsp::IsFlagSet()
{
	return (m_nUnknown48 >> 5) & 1;
}

float CWorldBsp::GetRadiusSqr()
{
	return m_fDiagonal;
}

WorldPolyRef* CWorldBsp::GetRoot()
{
	return m_n9c;
}

CWorldPart* CWorldBsp::FindPoly(HPOLY nHandle)
{
	uint32 iPart = nHandle & 0xffff;

	if (iPart < m_nParts)
		return m_ppParts[iPart];

	return LTNULL;
}

WorldPolyRef* CWorldBsp::GetPolyRefs()
{
	return m_n70;
}

uint32 CWorldBsp::IsBusy()
{
	return 0;
}

HPOLY CWorldBsp::MakeHPoly(WorldPolyRef* pRef)
{
    if (!pRef) return 0xffffffff;
    auto* pPoly = static_cast<WorldPolyRef*>(pRef)->m_pOwner;
    if (pPoly && pPoly->m_nIndex < m_nParts && m_ppParts[pPoly->m_nIndex] == pPoly)
        return (uint32(m_nf8) << 16) | pPoly->m_nIndex;

	return 0xffffffff;
}

IWorldBsp::IWorldBsp()
{
	m_Name[0] = 0;
}

CWorldSimple::CWorldSimple()
{
	m_n64 = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
	m_p7c = LTNULL;
}

CWorldSimple::~CWorldSimple()
{
	Reset();
}

void CWorldSimple::Reset()
{
	m_Array50.Term(&g_DefAlloc);

	m_n64 = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);

	m_Array68.Term(&g_DefAlloc);

	m_p7c = LTNULL;

    m_Sub80.Term();
    m_Header48 = {};
    m_vCenter.Init();
    m_vBoxMin.Init(); m_vBoxMax.Init();
    m_fRadiusSqr = 0;
    m_Name[0] = 0;
}

HPOLY CWorldSimple::MakeHPoly(WorldPolyRef* pPoly)
{
	return m_p7c ? m_p7c->MakeHPoly(pPoly) : 0xffffffff;
}

CWorldPart* CWorldSimple::FindPoly(HPOLY hPoly)
{
	return m_p7c ? m_p7c->FindPoly(hPoly) : nullptr;
}

CSubRecord* CWorldBsp::GetPointStatus(LTVector vPoint)
{
	return m_Sub108.GetPointStatus(vPoint);
}

float wb_MaxDistSqr(CWorldPart** ppParts, uint32 nParts, LTVector* pPoint)
{
	float        fMax = 0.0f;
	CWorldPart** pp;
	uint32       nLeft;
	uint32       j;

	if (nParts > 0)
	{
		pp    = ppParts;
		nLeft = nParts;

		do
		{
			CWorldPart* pPart = *pp;

			for (j = 0; j < pPart->m_nVerts; j++)
			{
				float f = pPoint->Dist(*pPart->m_Verts[j].m_pPos);

				if (f > fMax)
					fMax = f;
			}

			pp++;
		}
		while (--nLeft);
	}

	return fMax;
}

void CWorldSimple::ComputeRadius()
{
	uint32       nParts  = m_Array50.m_nElements;
	CWorldPart** ppParts = m_Array50.m_pArray;

    m_fRadiusSqr = wb_MaxDistSqr(ppParts, nParts, &m_vCenter);
    bool first = true;
    m_vBoxMin.Init(); m_vBoxMax.Init();
    for (uint32 i = 0; i < nParts; ++i) {
        const auto* part = ppParts[i];
        for (uint32 j = 0; j < part->m_nVerts; ++j) {
            const auto& point = *part->m_Verts[j].m_pPos;
            if (first) { m_vBoxMin = m_vBoxMax = point; first = false; }
            else {
                m_vBoxMin.x = std::min(m_vBoxMin.x, point.x); m_vBoxMax.x = std::max(m_vBoxMax.x, point.x);
                m_vBoxMin.y = std::min(m_vBoxMin.y, point.y); m_vBoxMax.y = std::max(m_vBoxMax.y, point.y);
                m_vBoxMin.z = std::min(m_vBoxMin.z, point.z); m_vBoxMax.z = std::max(m_vBoxMax.z, point.z);
            }
        }
    }
}

CSubRecord* CWorldSimple::GetPointStatus(LTVector vPoint)
{
	return m_Sub80.GetPointStatus(vPoint);
}

WorldPolyRef* CWorldSimple::GetPolyRefs()
{
	return m_Array68.m_pArray;
}

uint32 CWorldSimple::GetWorldFlags()
{
	return m_p7c ? m_p7c->GetWorldFlags() : 0;
}

float CWorldSimple::GetRadiusSqr()
{
	return m_fRadiusSqr;
}

uint32 CWorldSimple::IsBusy()
{
	return 1;
}

WorldPolyRef* CWorldSimple::GetRoot()
{
	return m_n64;
}

uint32 CWorldSimple::IsFlagSet()
{
	return 0;
}


CWorldSimple::CWorldSimple(const CWorldSimple& other)
    : IWorldBsp(other), m_Header48(other.m_Header48), m_Array50(other.m_Array50),
      m_n64(other.m_n64), m_Array68(other.m_Array68), m_p7c(other.m_p7c),
      m_Sub80(other.m_Sub80), m_vCenter(other.m_vCenter), m_fRadiusSqr(other.m_fRadiusSqr) {
    const auto remap = [&](WorldPolyRef* node) -> WorldPolyRef* {
        if (!node || node == g_pWorldTreeFrontTerm || node == g_pWorldTreeBackTerm) return node;
        const auto address = reinterpret_cast<std::uintptr_t>(node);
        const auto begin = reinterpret_cast<std::uintptr_t>(other.m_Array68.m_pArray);
        const auto length = std::size_t(other.m_Array68.m_nElements) * sizeof(WorldPolyRef);
        if (address < begin || address - begin >= length || (address - begin) % sizeof(WorldPolyRef))
            throw std::invalid_argument("world tree child is outside its node table");
        return m_Array68.m_pArray + (address - begin) / sizeof(WorldPolyRef);
    };
    m_n64 = remap(other.m_n64);
    for (uint32 i = 0; i < m_Array68.m_nElements; ++i)
        for (unsigned side = 0; side < 2; ++side) m_Array68[i].m_Children[side] = remap(other.m_Array68[i].m_Children[side]);
}
CWorldSimple& CWorldSimple::operator=(const CWorldSimple& other) {
    if (this == &other) return *this;
    for (uint32 i = 0; i < m_Array68.m_nElements; ++i) {
        const auto& head = m_Array68[i].m_head;
        if (head.m_pNext != &head || head.m_pPrev != &head)
            throw std::logic_error("cannot replace a world tree with linked objects");
    }
    CWorldSimple copy(other);
    Swap(copy);
    return *this;
}
void CWorldSimple::Swap(CWorldSimple& other) noexcept {
    using std::swap;
    swap(m_Name, other.m_Name); swap(m_vBoxMin, other.m_vBoxMin); swap(m_vBoxMax, other.m_vBoxMax);
    swap(m_Header48, other.m_Header48); swap(m_n64, other.m_n64); swap(m_p7c, other.m_p7c);
    m_Array50.Swap(other.m_Array50); m_Array68.Swap(other.m_Array68); m_Sub80.Swap(other.m_Sub80);
    swap(m_vCenter, other.m_vCenter); swap(m_fRadiusSqr, other.m_fRadiusSqr);
}


CServerWorldRec::CServerWorldRec()
{
	Clear();
}

CServerWorldRec::~CServerWorldRec()
{
	Term();
}

void CServerWorldRec::Clear()
{
	m_Flags     = 0;
	m_pWorldBsp = LTNULL;
	m_pOriginal = LTNULL;
	m_pCurrent  = LTNULL;
}

void CServerWorldRec::Term() {
    auto* current = m_Flags & 1 ? m_pWorldBsp : nullptr;
    auto* original = m_Flags & 2 ? m_pOriginal : nullptr;
    Clear();
    delete current;
    if (original != current) delete original;
}

bool w_ResolvePolyKeys(CWorldBsp& world, std::span<CServerWorldRec* const> models) {
    if (world.m_n8c && !world.m_p88) return false;
    try {
        std::vector<std::vector<CWorldPart*>> resolved(world.m_n8c);
        for (uint32 i = 0; i < world.m_n8c; ++i) {
            const auto& group = world.m_p88[i];
            if (group.m_nRefs && !group.m_pRefs) return false;
            auto& output = resolved[i];
            output.reserve(group.m_nRefs);
            for (uint32 j = 0; j < group.m_nRefs; ++j) {
                const auto key = group.m_pRefs[j];
                if (key.m_iModel >= models.size() || !models[key.m_iModel]) return false;
                const auto* model = models[key.m_iModel]->m_pWorldBsp;
                if (!model || !model->m_ppParts || key.m_iPoly >= model->m_nParts || !model->m_ppParts[key.m_iPoly]) return false;
                output.push_back(model->m_ppParts[key.m_iPoly]);
            }
        }
        for (uint32 i = 0; i < world.m_n8c; ++i) world.m_p88[i].m_Polygons = std::move(resolved[i]);
        return true;
    } catch (const std::bad_alloc&) { return false; }
}
void wm_ResetGeometry(CWorldBsp* world) {
    if (!world) return;
    for (uint32 i = 0; i < world->m_nParts; ++i) {
        auto* poly = world->m_ppParts[i];
        if (!poly) continue;
        poly->m_vCentre.Init(); poly->m_fRadius = 0;
        if (!poly->m_nVerts) continue;
        for (uint32 j = 0; j < poly->m_nVerts; ++j) poly->m_vCentre += *poly->m_Verts[j].m_pPos;
        poly->m_vCentre /= static_cast<float>(poly->m_nVerts);
        for (uint32 j = 0; j < poly->m_nVerts; ++j)
            poly->m_fRadius = std::max(poly->m_fRadius, poly->m_vCentre.Dist(*poly->m_Verts[j].m_pPos));
        poly->m_fRadius += 0.2f;
    }
    for (uint32 i = 0; i < world->m_n8c; ++i) {
        auto& group = world->m_p88[i];
        group.m_vCentre.Init(); group.m_fRadius = 0;
        uint64 vertices = 0;
        for (const auto* poly : group.m_Polygons) if (poly) {
            for (uint32 j = 0; j < poly->m_nVerts; ++j) group.m_vCentre += *poly->m_Verts[j].m_pPos;
            vertices += poly->m_nVerts;
        }
        if (vertices) group.m_vCentre /= static_cast<float>(vertices);
        for (const auto* poly : group.m_Polygons) if (poly)
            for (uint32 j = 0; j < poly->m_nVerts; ++j)
                group.m_fRadius = std::max(group.m_fRadius, group.m_vCentre.Dist(*poly->m_Verts[j].m_pPos));
        group.m_fRadius += 5.0f;
    }
}
