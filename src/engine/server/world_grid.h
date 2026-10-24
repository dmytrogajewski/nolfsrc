#pragma once
#include "ltbasedefs.h"
#include <span>
#include <vector>

class ILTStream;
struct WSNode {
    uint16 m_nPoly = 0;
    uint16 m_nChild[2]{0xfffe, 0xfffe};
};
struct CSubRecord {
    union { WSNode* m_pData = nullptr; WSNode* m_pNodes; };
    uint16 m_nCount = 0;
    union { uint16 m_nIndex = 0xfffe; uint16 m_n06; };
};
using CPhysicsBlock = CSubRecord;
struct WorldPolyRef;
struct PhysicsCandidates {
    bool multiple_cells = false;
    std::vector<const WSNode*> nodes;
};

class CWorldSub108 {
public:
    CWorldSub108() = default;
    ~CWorldSub108();
    CWorldSub108(const CWorldSub108&);
    CWorldSub108& operator=(const CWorldSub108&);
    CWorldSub108(CWorldSub108&&) noexcept;
    CWorldSub108& operator=(CWorldSub108&&) noexcept;
    void Clear() { Term(); }
    void Term();
    void Swap(CWorldSub108&) noexcept;
    LTBOOL Load(ILTStream*, const char** error = nullptr);
    CSubRecord* GetPointStatus(LTVector point);
    PhysicsCandidates QueryBox(LTVector minimum, LTVector maximum, std::span<WorldPolyRef> polygons) const;
    LTVector m_vCell{0,0,0}, m_vOrigin{0,0,0};
    int m_nCountX = 0, m_nCountY = 0, m_nCountZ = 0, m_nSliceStride = 0;
    uint32 m_nRecords = 0;
    CSubRecord* m_pRecords = nullptr;
};
