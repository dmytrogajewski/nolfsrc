#pragma once
#include "world_polygon.h"
#include <span>
#include <vector>

struct AxisClipPlane {
    uint32 axis;
    float limit, sign;
};
std::vector<LTVector> w_ClipPolygon(const CWorldPart&, std::span<const AxisClipPlane>);
bool w_PolygonTouchesBox(const CWorldPart&, LTVector minimum, LTVector maximum, float tolerance = 0);
