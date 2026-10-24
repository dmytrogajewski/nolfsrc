// *********************************************************************** //
//
//  MODULE:   SERVER (.CPP)
//
//  PURPOSE:  server.dll's two exports (exact signatures from
//            src/LT2/sdk/inc/server_interface.h), at 0x1002c6be and 0x1002cbb7.
//
//            There is no heap-allocated interface object: the returned
//            ServerInterface is one global MyServerInterface, and only the
//            CServerMgr behind it is new'd. Its m_pServerMgr is also the
//            "already instanced" flag, which is how a second CreateServer
//            gets refused.
//
// *********************************************************************** //

#define __SERVERAPI_EXPORT__
#include "servermgr.h"
#include "engine/portable/engine_resources.h"
#include <new>
#include <memory>
#include "platform/sync.h"
#include "platform/system.h"
#include "server_access.h"


// g_ServerInterface (0x10058bb0) is defined in servermgr.cpp: its initializer
// group 0x1002bfe7 sits at the head of that TU's code, before RunConsoleString.


// server.dll only. lithtech.exe has no export directory, so neither export
// is in it, and the interface they hand out (servermgr.cpp) is not either.
#ifdef LT_SERVER_BUILD
namespace {
nolf::platform::RecursiveMutex lifecycle_mutex;
bool lifecycle_transition = false;
struct Transition {
    Transition() { lifecycle_transition = true; }
    ~Transition() { lifecycle_transition = false; }
};
}
ServerInterfaceCall::ServerInterfaceCall()
    : lock_(lifecycle_mutex), manager_(g_ServerInterface.m_pServerMgr),
      activity_(manager_ ? manager_->m_Lifetime : nullptr) {}
SERVERAPI SI_CREATESTATUS CreateServer(int version, LTGUID &appGuid, ServerInterface **ppServer)
{
	if (!ppServer) return SI_ERRORINITTING;
	*ppServer = LTNULL;
	std::lock_guard lock(lifecycle_mutex);

	if (g_ServerInterface.m_pServerMgr || lifecycle_transition)
		return SI_ALREADYINSTANCED;

	if (version != SI_VERSION)
		return SI_INVALIDVERSION;

	if (!nolf::engine_resources_ready()) return SI_CANTLOADRESOURCEMODULE;

    Transition transition;
    try {
        auto manager = std::make_unique<CServerMgr>();
        if (!manager->Init()) return SI_ERRORINITTING;
        manager->m_NetMgr.m_Guid = appGuid;
        g_ServerInterface.m_pServerMgr = manager.release();
        *ppServer = &g_ServerInterface; return SI_OK;
    } catch (const std::exception& error) { nolf::platform::log("server creation",error.what()); }

	return SI_ERRORINITTING;
}


SERVERAPI void DeleteServer()
{
    std::lock_guard lock(lifecycle_mutex);
    if (lifecycle_transition) return;
	if (auto* manager = g_ServerInterface.m_pServerMgr)
	{
		lifecycle_transition = true;
		g_ServerInterface.m_pServerMgr = LTNULL;
        const auto life = manager->m_Lifetime;
        life->retiring = true;
        ++life->epoch;
        life->when_idle = [manager] {
            std::lock_guard lock(lifecycle_mutex);
            auto* classes = &manager->m_ClassMgr;
            delete manager;
            if (g_pServerMgr == manager) g_pServerMgr = nullptr;
            if (g_pClassMgr == classes) g_pClassMgr = nullptr;
            lifecycle_transition = false;
        };
        if (!life->active_calls) {
            auto cleanup = std::exchange(life->when_idle, {});
            cleanup();
        }
	}
}
#endif


// ---- the object map's constructor --------------------------------------- //
//
// 0x1002cbe3 — the manager's object-id map (CServerMgr+0x8fc) constructor.
// It sits in this TU's run of CServerMgr code (right after DeleteServer), but
// it is written once, as servermgr.h's CUnknown8FC::CUnknown8FC in smgr.cpp:
// the same 34-of-34-byte near (the shipped build sinks the first of Init's
// two zero pushes past three of the four field stores; cl 8168 and 8966
// schedule it exactly as 8447 does), calling the same Init at 0x1000c4cf. The
// local CObjectMapCtorView copy that stood here was a second definition of
// that one function under another class name, whose declared-only destructor
// and Init could not link, so it is gone. One thing its probing established
// is worth keeping: a member initialiser list puts the vptr store AFTER the
// member stores, where body assignments put it first, and the shipped order
// is vptr-then-members.
