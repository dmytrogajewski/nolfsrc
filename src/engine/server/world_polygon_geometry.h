#pragma once
#include "world_polygon.h"
#include "ltmatrix.h"
#include <vector>

struct WorldPolygonGeometry {
    LTPlane plane{LTVector(0,0,1),0};
    std::vector<LTVector> points;
};
LTVector world_TransformPoint(const LTMatrix&,LTVector);
WorldPolygonGeometry world_PolygonGeometry(const CWorldPart&,const LTMatrix& forward,const LTMatrix& inverse);
double world_PolygonDistanceSquared(const WorldPolygonGeometry&,LTVector);
