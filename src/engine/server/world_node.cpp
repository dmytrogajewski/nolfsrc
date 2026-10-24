#include "world_node.h"
#include "world_polygon.h"
namespace {
WorldPolyRef front_terminator(1), back_terminator(2);
}
void* g_pWorldTreeFrontTerm = &front_terminator;
void* g_pWorldTreeBackTerm = &back_terminator;
WorldPolyRef* w_GetTreeNodeQT1(WorldPolyRef* nodes, uint32 count, uint32 index) {
    if (index == 0xffffffff) return &front_terminator;
    if (index == 0xfffffffe) return &back_terminator;
    if (!nodes || index >= count) return nullptr;
    return nodes + index;
}

LTPlane* WorldPolyRef::GetPlane() { return m_pOwner->GetPlane(); }

void BspObjectLink::Bind(WorldPolyRef& node) {
    Detach();
    node.m_head.AddAfter(this);
    m_pNode = &node;
}
void BspObjectLink::Detach() { Remove(); m_pNode = nullptr; }
WorldPolyRef::~WorldPolyRef() {
    while (m_head.m_pNext != &m_head)
        static_cast<BspObjectLink*>(m_head.m_pNext)->Detach();
}

void w_SetNodeProjectionsQT1(void* pNodes, uint32 nNodes, int bCurrent)
{
	WorldPolyRef* pNode;
	LTPlane* pPlane;
	uint32 i;

	pNode = static_cast<WorldPolyRef*>(pNodes);

	for (i = 0; i < nNodes; i++)
	{
		pNode->m_nProject = 6;

		if (bCurrent)
		{
			pPlane = pNode->m_pOwner->m_pSurface->m_pPlane;

			if (pPlane->m_Normal.x > 0.99999f)
				pNode->m_nProject = 0;
			else if (pPlane->m_Normal.x < -0.99999f)
				pNode->m_nProject = 1;
			else if (pPlane->m_Normal.y > 0.99999f)
				pNode->m_nProject = 2;
			else if (pPlane->m_Normal.y < -0.99999f)
				pNode->m_nProject = 3;
			else if (pPlane->m_Normal.z > 0.99999f)
				pNode->m_nProject = 4;
			else if (pPlane->m_Normal.z < -0.99999f)
				pNode->m_nProject = 5;
		}

		pNode++;
	}
}
