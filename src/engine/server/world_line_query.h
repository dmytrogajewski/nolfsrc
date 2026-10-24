#pragma once
#include "world_bsp_interface.h"
#include "world_node.h"
#include <functional>

class IntersectQuery;
class IntersectInfo;
class CUnknown210;
LTBOOL w_IntersectSegment(IntersectQuery*,IntersectInfo*,CUnknown210*,int);
struct CLineQuery {
    LTVector* m_pStart = nullptr;
    LTVector* m_pEnd = nullptr;
    LTVector* m_pHit = nullptr;
    IntersectQuery* m_pQuery = nullptr;
    IWorldBsp* m_pWorldBsp = nullptr;
    WorldPoly* m_pPoly = nullptr;
    std::function<bool()> m_StillValid;
};
int w_PointInPoly(CWorldPart* polygon, LTVector* point);
int bspq_IntersectLineRec(BSPNode*, CLineQuery*, LTVector start, LTVector end);
int bspq_IntersectLine(BSPNode*, CLineQuery*);
WorldPoly* bsp_IntersectLine(BSPNode*, LTVector* start, LTVector* end, LTVector* hit, LTPlane* plane);
