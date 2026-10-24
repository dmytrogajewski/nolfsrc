#pragma once
#include "world_polygon.h"
#include "ltlink.h"
#include <vector>

struct CWorldPolyKey {
    uint16 m_iModel = 0;
    uint16 m_iPoly = 0;
};
static_assert(sizeof(CWorldPolyKey) == 4);
struct CWorldGroupEntryQT1 {
    uint16 m_n00 = 0, m_nBytes = 0;
    void* m_pData = nullptr;
};
struct CWorldGroupQT1 {
    CWorldGroupQT1() { m_Link.TieOff(); m_Link.m_pData = nullptr; }
    ~CWorldGroupQT1() {
        while (m_Link.m_pNext != &m_Link) m_Link.m_pNext->Remove();
    }
    LTVector m_vCentre{0,0,0};
    float m_fRadius = 0;
    CWorldGroupEntryQT1* m_pEntries = nullptr;
    LTLink m_Link;
    CWorldPolyKey* m_pRefs = nullptr;
    uint32 m_nRefs = 0, m_n28 = 0;
    uint16 m_Unknown2c = 0, m_nEntries = 0;
    // Original file keys remain four bytes. Resolved pointers own separate
    // native storage and can be replaced without corrupting those keys.
    std::vector<CWorldPart*> m_Polygons;
};
class WorldPortal {
public:
    char* m_pName = nullptr;
    uint16 m_n04 = 0, m_Flags = 0, m_nIndex = 0;
    LTVector m_v0c{0,0,0}, m_v18{0,0,0};
};
using CWorldChunk = WorldPortal;
