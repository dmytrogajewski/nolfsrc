#include "servermgr.h"
#include "servermsg.h"
#include "platform/system.h"

LPBASECLASS sm_AllocateObject(CServerMgr* server, ClassDef* definition) {
    return server->m_ClassMgr.allocate(definition);
}

int cb_LoadModule(CClassMgr* registry) {
    if (!registry || !registry->m_pServerMgr) return LT_INVALIDPARAMS;
    auto* server = registry->m_pServerMgr;
    ServerCallbackGuard operation(server->m_Lifetime);
    auto result = registry->load(server->m_pUnknown928);
    if (!operation.unchanged()) return LT_ERROR;
    if (result != LT_OK) return result;
    for (int i = 0; i < registry->m_nClasses; ++i) {
        auto& record = registry->m_pClassStats[i];
        if (!(record.m_pClassDef->m_ClassFlags & CF_STATIC)) continue;
        auto* object = registry->allocate(record.m_pClassDef);
        if (!operation.unchanged()) return LT_ERROR;
        if (!object) { registry->Term(); return LT_ERROR; }
        ObjectCreateStruct create;
        result = sm_CreateObjectInWorld(server, object, record.m_pClassDef, &create,
                                       0xffff, 0.0f, &record.m_pStaticObject);
        if (!operation.unchanged()) return LT_ERROR;
        if (result != LT_OK) {
            registry->destroy(object);
            nolf::platform::log("game module", "failed to attach static game object");
            registry->Term(); return result;
        }
    }
    return LT_OK;
}
