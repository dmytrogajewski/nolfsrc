#include "world_line_query.h"
#include "world_polygon.h"
#include <cmath>
#include <vector>

int w_PointInPoly(CWorldPart* polygon, LTVector* point) {
    if (!polygon || !point || !polygon->m_pSurface || !(polygon->m_pSurface->m_nFlags & 1) || polygon->m_nVerts < 3) return 0;
    if (!std::isfinite(point->x) || !std::isfinite(point->y) || !std::isfinite(point->z)) return 0;
    const auto delta = polygon->m_vCentre - *point;
    if (delta.MagSqr() > polygon->m_fRadius * polygon->m_fRadius) return 0;
    const auto& normal = polygon->m_pSurface->m_pPlane->m_Normal;
    auto* previous = polygon->m_Verts + polygon->m_nVerts - 1;
    for (uint32 i = 0; i < polygon->m_nVerts; ++i) {
        auto* current = polygon->m_Verts + i;
        auto edge_normal = normal.Cross(*previous->m_pPos - *current->m_pPos);
        const auto length = edge_normal.Mag();
        if (length > 0) {
            edge_normal /= length;
            if (edge_normal.Dot(*point) - edge_normal.Dot(*current->m_pPos) < -.01f) return 0;
        }
        previous = current;
    }
    return 1;
}
int bspq_IntersectLineRec(BSPNode* node, CLineQuery* query, LTVector start, LTVector end) {
    if (!node || !query || !query->m_pHit) return 0;
    if (query->m_StillValid && !query->m_StillValid()) return 0;
    const auto finite = [](LTVector p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if (!finite(start) || !finite(end)) return 0;
    struct Frame { BSPNode* node; LTVector middle, end; int side; float fraction; };
    std::vector<Frame> pending;
    for (;;) {
        while (!(node->m_Flags & 3)) {
            const auto* plane = node->GetPlane();
            const auto distance = [&](const LTVector& point) {
                float result = point.z * plane->m_Normal.z;
                result += point.y * plane->m_Normal.y; result += point.x * plane->m_Normal.x;
                return result - plane->m_Dist;
            };
            const float first = distance(start), last = distance(end);
            if (first > .01f && last > .01f) { node = node->m_pChild[1]; continue; }
            if (first < -.01f && last < -.01f) { node = node->m_pChild[0]; continue; }
            const int side = first <= .01f && first >= -.01f ? (last < 0 ? 1 : 0) : (first > 0 ? 1 : 0);
            const float difference = last - first;
            if (difference != 0 && (first < -.01f || first > .01f)) {
                const float fraction = -first / difference;
                const auto middle = (end - start) * fraction + start;
                pending.push_back({node, middle, end, side, fraction});
                node = node->m_pChild[side]; end = middle;
            } else node = node->m_pChild[side == 0];
        }
        bool resume = false;
        while (!pending.empty()) {
            const auto frame = pending.back(); pending.pop_back();
            if (frame.side == 1 && frame.node->m_pOwner) {
                auto point = frame.middle;
                if (w_PointInPoly(frame.node->m_pOwner, &point)) {
                    bool accepted = true;
                    if (query->m_pQuery && query->m_pQuery->m_PolyFilterFn && query->m_pWorldBsp)
                        accepted = query->m_pQuery->m_PolyFilterFn(query->m_pWorldBsp->MakeHPoly(frame.node), query->m_pQuery->m_pUserData);
                    if (query->m_StillValid && !query->m_StillValid()) return 0;
                    if (accepted) {
                    query->m_pPoly = frame.node; *query->m_pHit = frame.middle; return 1;
                    }
                }
            } else if (frame.fraction > .99f) continue;
            start = frame.middle; end = frame.end; node = frame.node->m_pChild[frame.side == 0];
            resume = true; break;
        }
        if (!resume) return 0;
    }
}
int bspq_IntersectLine(BSPNode* root, CLineQuery* query) {
    return query && query->m_pStart && query->m_pEnd ? bspq_IntersectLineRec(root, query, *query->m_pStart, *query->m_pEnd) : 0;
}
WorldPoly* bsp_IntersectLine(BSPNode* root, LTVector* start, LTVector* end, LTVector* hit, LTPlane* plane) {
    CLineQuery query; query.m_pStart = start; query.m_pEnd = end; query.m_pHit = hit;
    if (!bspq_IntersectLine(root, &query)) return nullptr;
    if (plane) *plane = *query.m_pPoly->GetPlane();
    return query.m_pPoly;
}
