#include "servermgr.h"

namespace {
bool live(CServerMgr* manager, LTObject* object) {
    return manager && object && manager->m_Unknown390.m_Allocated.contains(object) && object->m_pSData;
}
void notify(void* context, LTObject* owner, LTObject* linked) {
    auto* manager = static_cast<CServerMgr*>(context);
    if (live(manager,owner) && owner->m_pSData->m_pBaseClass)
        manager->m_ClassMgr.message(owner->m_pSData->m_pBaseClass,MID_LINKBROKEN,linked,0);
}
}
LTRESULT sm_CreateInterObjectLink(CServerMgr* manager, LTObject* owner, LTObject* linked, int type) {
    if (!live(manager,owner) || !live(manager,linked) || type < 0 || type > 1) return LT_INVALIDPARAMS;
    try { return manager->m_InterObjectLinks.Create(owner,linked,type) ? LT_OK : LT_ERROR; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
}
LTRESULT sm_AttachSound(CServerMgr* manager, LTObject* owner, SoundInstance* sound) {
    if (!live(manager,owner) || !manager->m_SoundInstances.Contains(sound)) return LT_INVALIDPARAMS;
    try { return manager->m_InterObjectLinks.Attach(owner,sound) ? LT_OK : LT_ERROR; }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
}
void sm_BreakInterObjectLink(CServerMgr* manager, LTObject* owner, LTObject* linked, int all) {
    if (manager) manager->m_InterObjectLinks.Break(owner,linked,all != 0);
}
void sm_DetachSound(CServerMgr* manager, LTObject* owner, SoundInstance* sound, int all) {
    if (manager) manager->m_InterObjectLinks.Detach(owner,sound,all != 0);
}
void sm_BreakAttachments(CServerMgr* manager, LTObject* object, int type, int send_notification) {
    if (manager && type >= 0 && type <= 2)
        manager->m_InterObjectLinks.BreakFor(object,type,send_notification ? notify : nullptr,manager);
}
