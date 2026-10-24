#pragma once
#include "ltobject_internal.h"

struct PointContainerGather {
    HOBJECT* m_pList = nullptr;
    uint32 m_MaxListSize = 0, m_nElements = 0;
    LTVector m_Position{0,0,0};
};
bool object_IsPointInContainer(const LTObject&, LTVector);
int sm_GatherContainersCB(LTObject*, void*);
