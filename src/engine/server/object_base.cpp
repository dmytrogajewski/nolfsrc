#include "ltobject_internal.h"
#include "commonmodel.h"
#include "object_pool.h"
#include "object_manager.h"
#include "servermath.h"

LTObject::LTObject(uint8 type) : m_ObjectType(type) {
    m_Link6c.m_pData = this;
    for (auto* link : std::array<LTLink*,7>{&m_Link60, &m_Link6c, &m_Link7c, &m_StandingOnMe,
                                          &m_StandingLink, &m_Link150, &m_Link170}) link->Init2(this);
    m_Rotation.Init();
    UpdateBounds();
}

LTObject::~LTObject() {
    *m_Lifetime = false;
    obj_DetachAll(m_pAttachMgr, this);
    obj_UnlinkTrackersB(this);
    obj_UnlinkTrackersA(this);
    m_Link150.Remove();
    m_Link170.Remove();
    Unlink();
}

void LTObject::_v20(CUnknown390* manager, ObjectCreateStruct* create) {
    m_pAttachMgr = manager;
    m_Flags = create->m_Flags;
    m_Flags2 = create->m_Flags2;
    m_Pos = create->m_Pos;
    m_Rotation = create->m_Rotation;
    m_Scale = create->m_Scale;
    m_ObjectType = static_cast<uint8>(create->m_ObjectType);
    UpdateBounds();
}

LTBOOL LTObject::_v04(CObjVisitor* visitor) {
    if (!(m_Flags & 0x40) || !visitor) return LTFALSE;
    visitor->Visit(this);
    return LTTRUE;
}
void LTObject::_v08() { obj_ReleaseMemory(this); Unlink(); }
void LTObject::BaseTerm() { Unlink(); }
void LTObject::Unlink() {
    m_SpatialMembership = SpatialMembership{};
    m_Link60.Remove();
    m_Link6c.Detach();
}
void LTObject::GetDims(LTVector* min, LTVector* max) { *min = m_BBoxMin; *max = m_BBoxMax; }
LTBOOL LTObject::IsInWorld() { return m_pSData != nullptr; }
void LTObject::GetTransformMatrix(LTMatrix* out) { mat_SetupTransform(&m_Pos, &m_Rotation, &m_Scale, out); }
float LTObject::_v2c() { return m_Radius; }
LTBOOL LTObject::_v30() { return LTTRUE; }
void LTObject::SetupModelWalker(ModelWalker* walker) { walker->m_Unknown_e0 = 0; }
void LTObject::SetupModelWalkerRefsOnly(ModelWalker* walker) { SetupModelWalker(walker); }
LTBOOL LTObject::FindTracker(LTAnimTracker*, LTAnimTracker*** link) {
    if (link) *link = nullptr;
    return LTFALSE;
}
float LTObject::GetScaledRadius() { return _v2c(); }
void LTObject::UpdateBounds() {
    m_BBoxMin = m_Pos - m_Dims;
    m_BBoxMax = m_Pos + m_Dims;
    m_Radius = m_Dims.Mag() + .1f;
}
void LTObject::SetPos(LTVector position) { m_Pos = position; UpdateBounds(); }
void LTObject::SetDims(LTVector dimensions) { m_Dims = dimensions; UpdateBounds(); }

void obj_DetachAll(CUnknown390* manager, LTObject* object) {
    if (manager) am_DetachObject(manager,object);
    auto* current = object->m_pAttachments;
    object->m_pAttachments = nullptr;
    while (current) {
        auto* next = current->m_pNext;
        current->~Attachment();
        sb_Free(object->m_pAttachmentBank, current);
        current = next;
    }
}

void obj_ReleaseMemory(LTObject* object) {
    object->Unlink();
    if (object->m_ObjectType == OT_POLYGRID) pg_FreeData(object);
}
void pg_FreeData(LTObject* object) {
    auto& head = static_cast<CObjPolyGrid*>(object)->m_Records;
    while (head.m_pNext != &head) {
        auto* record = static_cast<PGRecord*>(head.m_pNext->m_pData);
        record->m_Link.Remove();
        record->m_Link0c.Remove();
        record->~PGRecord();
        sb_Free(&g_MemTrackBank, record);
    }
}

void obj_UnlinkTrackersA(LTObject* object) {
    object->m_StandingLink.Remove();
    object->m_pStandingOn = nullptr;
    object->m_pStandingOnPoly = nullptr;
}
void obj_UnlinkTrackersB(LTObject* object) {
    auto& head = object->m_StandingOnMe;
    while (head.m_pNext != &head) {
        auto* standing = static_cast<LTObject*>(head.m_pNext->m_pData);
        obj_UnlinkTrackersA(standing);
        standing->m_ChangeFlags |= CHANGEFLAG_MOTION;
    }
}
void obj_SetStandingOn(LTObject* object, LTObject* on, WorldPoly* polygon) {
    obj_UnlinkTrackersA(object);
    if (on) on->m_StandingOnMe.AddAfter(&object->m_StandingLink);
    object->m_pStandingOn = on;
    object->m_pStandingOnPoly = polygon;
}
