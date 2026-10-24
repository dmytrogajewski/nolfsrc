#include "world_containment.h"
#include "world_polygon.h"
#include <cmath>
#include <vector>

namespace {
bool finite(const LTVector* point) {
    return point && std::isfinite(point->x) && std::isfinite(point->y) && std::isfinite(point->z);
}
float distance(const LTPlane& plane, const LTVector& point) {
    return point.y * plane.m_Normal.y + point.z * plane.m_Normal.z +
        point.x * plane.m_Normal.x - plane.m_Dist;
}
}
int bsp_PointInside(BSPNode* node, LTVector* point) {
    if (!node || !finite(point)) return 0;
    for (;;) {
        if (node->m_Flags & 2) return 0;
        if (node->m_Flags & 1) return 1;
        node = node->m_pChild[distance(*node->GetPlane(), *point) > 0 ? 1 : 0];
    }
}
int bsp_SphereInside(BSPNode* node, LTVector* point, float radius) {
    if (!node || !finite(point) || !std::isfinite(radius) || radius < 0) return 0;
    std::vector<BSPNode*> pending;
    for (;;) {
        while (node->m_Flags & 3) {
            if (pending.empty()) return node->m_Flags & 1;
            node = pending.back(); pending.pop_back();
        }
        const auto dist = distance(*node->GetPlane(), *point);
        if (dist > radius) node = node->m_pChild[1];
        else if (dist < -radius) node = node->m_pChild[0];
        else {
            auto* back = node->m_pChild[0];
            if (back->m_Flags & 2) return 2;
            pending.push_back(back);
            node = node->m_pChild[1];
        }
    }
}
