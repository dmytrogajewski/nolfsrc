#include "engine/portable/game_module.h"

extern "C" ClassDef** ObjectDLLSetup(int*, ILTServer*, int*);
extern "C" NOLF_EXPORT const nolf::platform::ModuleHeader* nolf_module_setup(uint32 version) {
    static constexpr auto api = nolf::server_module_api(ObjectDLLSetup, CreateServerShell, DeleteServerShell,
                                                       nolf::platform::set_log_sink);
    return version == api.header.version ? &api.header : nullptr;
}
