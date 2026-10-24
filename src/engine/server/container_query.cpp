#include "container_query.h"
#include "world_containment.h"
#include "servermath.h"
#include <cmath>

bool object_IsPointInContainer(const LTObject& object, LTVector point) {
    if (object.m_Unknown54 || object.m_ObjectType != OT_CONTAINER || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return false;
    const auto* container = dynamic_cast<const CObjContainer*>(&object);
    auto* geometry = object.m_pWorldBsp;
    if (!container || !geometry || geometry->IsBusy() || !geometry->GetRoot()) return false;
    if (point.DistSqr(object.m_Pos) > geometry->GetBoxDiagonalSqr()) return false;
    LTVector local;
    auto inverse = container->m_Transform230;
    MatVMul(&local,&inverse,&point);
    return bsp_PointInside(geometry->GetRoot(),&local) == 0;
}
int sm_GatherContainersCB(LTObject* object, void* context) {
    auto* gather = static_cast<PointContainerGather*>(context);
    if (!object || !gather || !gather->m_pList || gather->m_nElements >= gather->m_MaxListSize) return 1;
    if (object_IsPointInContainer(*object,gather->m_Position)) gather->m_pList[gather->m_nElements++] = object;
    return 1;
}
