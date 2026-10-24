#include "world_line_query.h"
#include "worldbsp.h"
#include "world_spatial.h"
#include "object_lifetime.h"
#include "counter.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <new>
#include <vector>

thread_local float g_Unknown585a0 = 0;
thread_local uint32 g_nIsectQueries = 0, g_nIsectTime = 0;

namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool same_transform(const LTMatrix& first, const LTMatrix& second) {
    for (unsigned row = 0; row < 4; ++row) for (unsigned column = 0; column < 4; ++column)
        if (first.m[row][column] != second.m[row][column]) return false;
    return true;
}
// Ordinary objects use their world-space box. Rays starting inside a box
// retain the original exclusion behavior; their own actor is filtered out.
bool box_hit(const IntersectQuery& query, const LTObject& object, IntersectInfo& output) {
    const auto direction = query.m_To-query.m_From;
    float enter = 0, leave = 1;
    int face = -1;
    float sign = 0;
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) {
            if (query.m_From[axis] < object.m_BBoxMin[axis] || query.m_From[axis] > object.m_BBoxMax[axis]) return false;
            continue;
        }
        auto first = (object.m_BBoxMin[axis]-query.m_From[axis])/direction[axis];
        auto last = (object.m_BBoxMax[axis]-query.m_From[axis])/direction[axis];
        float normal = -1;
        if (first > last) { std::swap(first,last); normal = 1; }
        if (first > enter) { enter = first; face = axis; sign = normal; }
        leave = std::min(leave,last);
        if (enter > leave) return false;
    }
    if (face < 0 || enter > 1) return false;
    output.m_Point = query.m_From+direction*enter;
    output.m_Plane.m_Normal[face] = sign;
    output.m_Plane.m_Dist = output.m_Plane.m_Normal.Dot(output.m_Point);
    return finite(output.m_Point);
}
struct Hit {
    float distance;
    IntersectInfo info;
    std::function<bool()> valid;
};
}

// One query owns its complete state. Filters can issue nested segment queries,
// remove actors or replace geometry without changing the outer query's cursor.
LTBOOL w_IntersectSegment(IntersectQuery* input, IntersectInfo* output, CUnknown210* objects, int) {
    if (!input || !output || !objects) return LTFALSE;
    const IntersectQuery query = *input;
    *output = IntersectInfo{};
    const auto length_squared = query.m_From.DistSqr(query.m_To);
    if (!finite(query.m_From) || !finite(query.m_To) || !std::isfinite(length_squared) || length_squared <= 0) return LTFALSE;
    ++g_nIsectQueries;
#ifdef LT_SERVER_TIMERS
    CBaseCounter timer(0);
    struct Timing { CBaseCounter* timer; ~Timing() { g_nIsectTime += cnt_EndCounter(timer); } } timing{&timer};
    cnt_StartCounter(&timer);
#endif
    try {
        std::vector<Hit> hits;
        objects->QuerySegment(query.m_From,query.m_To,[&](LTObject* object) {
            ObjectCallbackGuard actor(*object);
            if ((query.m_Flags & IGNORE_NONSOLID) && !(object->m_Flags & (FLAG_RAYHIT|FLAG_SOLID))) return;
            if (!(query.m_Flags & INTERSECT_OBJECTS) && !object->_v34()) return;
            if (!actor.unchanged()) return;
            if (query.m_FilterFn) {
                const auto accepted = query.m_FilterFn(object,query.m_pUserData);
                if (!actor.unchanged() || !accepted) return;
            }
            IntersectInfo info; info.m_hObject = object;
            std::function<bool()> valid = [actor] { return actor.unchanged(); };
            if (object->m_ObjectType == OT_WORLDMODEL || object->m_ObjectType == OT_CONTAINER) {
                auto* model = static_cast<WorldModelObject*>(object);
                auto* bsp = object->m_pWorldBsp;
                if (!bsp) return;
                const auto geometry = bsp->m_GeometryLifetime;
                const auto generation = geometry->generation;
                auto transform = model->m_Transform1f0;
                valid = [actor,geometry,generation,model,bsp,transform] {
                    return actor.unchanged() && geometry->Matches(generation) && model->m_pWorldBsp == bsp &&
                           same_transform(model->m_Transform1f0,transform);
                };
                if (!valid()) return;
                LTVector from, to, point;
                model->m_Transform230.Apply(query.m_From,from);
                model->m_Transform230.Apply(query.m_To,to);
                if (!finite(from) || !finite(to)) return;
                IntersectQuery filtered = query;
                CLineQuery line;
                line.m_pStart = &from; line.m_pEnd = &to; line.m_pHit = &point;
                line.m_pQuery = query.m_Flags & INTERSECT_HPOLY ? &filtered : nullptr;
                line.m_pWorldBsp = bsp; line.m_StillValid = valid;
                if (!bspq_IntersectLine(bsp->GetRoot(),&line) || !valid() || !line.m_pPoly || !line.m_pPoly->m_pOwner) return;
                transform.Apply(point,info.m_Point);
                const auto& plane = *line.m_pPoly->GetPlane();
                transform.Apply3x3(plane.m_Normal,info.m_Plane.m_Normal);
                LTVector plane_point;
                transform.Apply(plane.m_Normal*plane.m_Dist,plane_point);
                info.m_Plane.m_Dist = info.m_Plane.m_Normal.Dot(plane_point);
                info.m_SurfaceFlags = line.m_pPoly->m_pOwner->m_pSurface->m_nTextureFlags;
                if (query.m_Flags & INTERSECT_HPOLY) info.m_hPoly = bsp->MakeHPoly(line.m_pPoly);
            } else if (!box_hit(query,*object,info)) return;
            const auto distance = query.m_From.DistSqr(info.m_Point);
            if (std::isfinite(distance) && distance < length_squared+1.f && finite(info.m_Plane.m_Normal))
                hits.push_back({distance,info,std::move(valid)});
        });
        // A later filter can retire an earlier, nearer hit. Keep all candidates
        // so the closest surviving identity still wins in that case.
        const Hit* best = nullptr;
        for (const auto& hit : hits) if (hit.valid() && (!best || hit.distance < best->distance)) best = &hit;
        if (!best) return LTFALSE;
        *output = best->info;
        return LTTRUE;
    } catch (const std::bad_alloc&) { return LTFALSE; }
}
