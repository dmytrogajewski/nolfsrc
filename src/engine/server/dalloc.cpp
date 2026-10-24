// Allocation failure reporting. Raw storage and pools own their allocation
// implementations in portable/allocation.cpp and structbank.cpp.
#include "servermgr.h"
#include "platform/system.h"
#include <new>

void dsi_OutOfMemory() {
    if (g_pServerMgr && g_pServerMgr->m_pAppHandler)
        g_pServerMgr->m_pAppHandler->OutOfMemory();
    nolf::platform::log("engine", "Out of memory");
    throw std::bad_alloc();
}
