#include "world_clip.h"
#include <array>

std::vector<LTVector> w_ClipPolygon(const CWorldPart& polygon, std::span<const AxisClipPlane> planes) {
    std::vector<LTVector> input, output;
    input.reserve(std::size_t(polygon.m_nVertices)+planes.size());
    output.reserve(std::size_t(polygon.m_nVertices)+planes.size());
    for (uint32 i = 0; i < polygon.m_nVertices; ++i) input.push_back(*polygon.m_Vertices[i].m_pPos);
    for (const auto& plane : planes) {
        if (input.empty()) break;
        output.clear();
        auto previous = input.back();
        double previous_distance = plane.sign*(double(previous[plane.axis])-plane.limit);
        for (const auto current : input) {
            const double distance = plane.sign*(double(current[plane.axis])-plane.limit);
            if ((distance >= 0) != (previous_distance >= 0)) {
                const double fraction = previous_distance/(previous_distance-distance);
                LTVector point;
                for (uint32 axis = 0; axis < 3; ++axis) point[axis] = float(double(previous[axis])+(double(current[axis])-previous[axis])*fraction);
                output.push_back(point);
            }
            if (distance >= 0) output.push_back(current);
            previous = current; previous_distance = distance;
        }
        input.swap(output);
    }
    return input;
}
bool w_PolygonTouchesBox(const CWorldPart& polygon, LTVector minimum, LTVector maximum, float tolerance) {
    const std::array<AxisClipPlane,6> planes{{
        {0,minimum.x-tolerance,1},{0,maximum.x+tolerance,-1},
        {1,minimum.y-tolerance,1},{1,maximum.y+tolerance,-1},
        {2,minimum.z-tolerance,1},{2,maximum.z+tolerance,-1}}};
    return !w_ClipPolygon(polygon,planes).empty();
}
