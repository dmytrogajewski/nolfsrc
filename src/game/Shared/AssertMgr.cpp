#include "Stdafx.h"
#include "AssertMgr.h"
#include "engine/portable/string.h"
#include "platform/diagnostics.h"

std::atomic_bool CAssertMgr::enabled_{false};
void CAssertMgr::Enable() { enabled_ = true; }
void CAssertMgr::Disable() { enabled_ = false; }

void CAssertMgr::ReportFailed(const char* expression, const char* file, unsigned line) {
    if (enabled_) {
#ifdef _CLIENTBUILD
        auto* api = g_pLTClient;
        const auto variable = api ? api->GetConsoleVar("assert") : nullptr;
#else
        auto* api = g_pLTServer;
        const auto variable = api ? api->GetGameConVar("assert") : nullptr;
#endif
        const char* mode = variable ? api->GetVarValueString(variable) : nullptr;
        if (mode && nolf::ascii_compare(mode, "null") == 0) return;
        if (api && (!mode || nolf::ascii_compare(mode, "console") == 0)) {
            api->CPrint("Assertion %s (%s:%u)", expression, file, line);
            return;
        }
    }
    nolf::platform::assertion_failed(expression, file, line);
}
