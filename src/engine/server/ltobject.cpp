#include "ltobject_internal.h"
#include "worldbsp.h"
#include "object_pool.h"
#include "servermath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

HPOLY LTObject::MakeHPoly(WorldPoly* polygon) {
    if (m_pOriginalBsp) {
        const auto handle = m_pOriginalBsp->MakeHPoly(polygon);
        if (handle != INVALID_HPOLY) return handle;
    }
    return m_pWorldBsp ? m_pWorldBsp->MakeHPoly(polygon) : INVALID_HPOLY;
}
void obj_UpdateTransform(LTObject* object) {
    auto* world = dynamic_cast<CWorldBsp*>(object->m_pWorldBsp);
    if (!world) return;
    auto* model = static_cast<WorldModelObject*>(object);
    wm_BuildTransforms(&world->m_ve8, &object->m_Pos, &object->m_Rotation,
                       &model->m_Transform1f0, &model->m_Transform230);
}
void obj_RetransformWorldModel(LTObject* object) {
    obj_UpdateTransform(object);
    wm_Transform(object, &static_cast<WorldModelObject*>(object)->m_Transform1f0,
                 (object->m_Flags >> 14) & 1);
}
void wm_Transform(LTObject* object, LTMatrix* matrix, int box_physics) {
    auto* source = dynamic_cast<CWorldBsp*>(object->m_pWorldBsp);
    auto* target = dynamic_cast<CWorldBsp*>(object->m_pOriginalBsp);
    if (!source || !target || source == target || box_physics ||
        source->m_nac != target->m_nac || source->m_n6c != target->m_n6c || source->m_nParts != target->m_nParts) return;
    auto* from_points = static_cast<LTVector*>(source->m_pa8);
    auto* to_points = static_cast<LTVector*>(target->m_pa8);
    for (uint32 i = 0; i < source->m_nac; ++i) MatVMul(to_points + i, matrix, from_points + i);
    auto* from_planes = static_cast<LTPlane*>(source->m_p68);
    auto* to_planes = static_cast<LTPlane*>(target->m_p68);
    for (uint32 i = 0; i < source->m_n6c; ++i) {
        auto point = from_planes[i].m_Normal * from_planes[i].m_Dist;
        MatVMul_InPlace(matrix, &point);
        MatVMul_3x3(&to_planes[i].m_Normal, matrix, &from_planes[i].m_Normal);
        to_planes[i].m_Dist = to_planes[i].m_Normal.Dot(point);
    }
    for (uint32 i = 0; i < source->m_nParts; ++i)
        MatVMul(&target->m_ppParts[i]->m_vCentre, matrix, &source->m_ppParts[i]->m_vCentre);
    LTVector min, max;
    for (uint32 corner = 0; corner < 8; ++corner) {
        LTVector point(corner & 1 ? source->m_vBoxMax.x : source->m_vBoxMin.x,
                       corner & 2 ? source->m_vBoxMax.y : source->m_vBoxMin.y,
                       corner & 4 ? source->m_vBoxMax.z : source->m_vBoxMin.z);
        MatVMul_InPlace(matrix, &point);
        if (!corner) min = max = point;
        else for (uint32 axis = 0; axis < 3; ++axis) {
            min[axis] = std::min(min[axis],point[axis]); max[axis] = std::max(max[axis],point[axis]);
        }
    }
    target->m_vBoxMin = min; target->m_vBoxMax = max;
    target->Recalc();
}

namespace {
WorldPolyRef* sphere_node(WorldPolyRef* node, const LTVector& position, float radius) {
    while (node && !(node->m_flag & 3)) {
        const auto distance = node->GetPlane()->DistTo(position);
        if (distance > radius) {
            auto* next = node->m_Children[1];
            if (next && (next->m_flag & 1)) return node;
            node = next;
        } else if (distance < -radius) node = node->m_Children[0];
        else return node;
    }
    return nullptr;
}
bool place_grid(CWorldBsp& world, CObjPolyGrid& grid) {
    std::array<LTVector,8> corners;
    LTMatrix transform; grid.GetTransformMatrix(&transform);
    const LTVector half(float(grid.m_Width)*.5f,128,float(grid.m_Height)*.5f);
    for (uint32 i = 0; i < corners.size(); ++i) {
        corners[i].Init(i & 1 ? half.x : -half.x,i & 2 ? half.y : -half.y,i & 4 ? half.z : -half.z);
        MatVMul_InPlace(&transform,&corners[i]);
    }
    std::vector<WorldPolyRef*> stack{world.GetRoot()};
    std::vector<uint8> groups(world.m_n8c);
    while (!stack.empty()) {
        auto* node = stack.back(); stack.pop_back();
        if (!node || (node->m_flag & 3)) continue;
        if (node->m_x != 0xffff) {
            if (node->m_x < groups.size()) groups[node->m_x] = 1;
            continue;
        }
        bool front = false, back = false;
        for (const auto& corner : corners) {
            if (node->GetPlane()->DistTo(corner) > 0) front = true; else back = true;
        }
        if (front) stack.push_back(node->m_Children[1]);
        if (back) stack.push_back(node->m_Children[0]);
    }
    // Reserve every record before linking any of them. An allocation failure
    // leaves neither the grid nor a world group with a partial membership.
    std::vector<PGRecord*> records;
    records.reserve(std::count(groups.begin(),groups.end(),uint8(1)));
    for (uint32 group = 0; group < groups.size(); ++group) if (groups[group]) {
        auto* memory = sb_Allocate(&g_MemTrackBank);
        if (!memory) {
            for (auto* record : records) { record->~PGRecord(); sb_Free(&g_MemTrackBank,record); }
            return false;
        }
        auto* record = new (memory) PGRecord;
        record->m_Link.Init2(record); record->m_Link0c.Init2(record);
        record->m_pObject = &grid; record->m_pLeaf = world.m_p88 + group;
        records.push_back(record);
    }
    for (auto* record : records) {
        static_cast<CWorldGroupQT1*>(record->m_pLeaf)->m_Link.AddAfter(&record->m_Link);
        grid.m_Records.AddAfter(&record->m_Link0c);
    }
    return true;
}
}
void* obj_LinkIntoWorldTree(CWorldBsp* world, LTObject* object) {
    if (!world || !object) return nullptr;
    obj_ReleaseMemory(object);
    auto* node = sphere_node(world->GetRoot(),object->m_Pos,object->m_Radius);
    if (!node) return nullptr;
    if (object->m_ObjectType == OT_POLYGRID && !place_grid(*world,*static_cast<CObjPolyGrid*>(object))) return nullptr;
    object->m_Link6c.Bind(*node);
    return node;
}
