#include "Stdafx.h"
#include "engine/portable/client_module.h"
#include "GameClientShell.h"

namespace {
void input(IClientShell* shell,const nolf::platform::InputEvent& event) {
    static_cast<CGameClientShell*>(shell)->HandleWindowInput(event);
}
}
extern "C" NOLF_EXPORT const nolf::platform::ModuleHeader* nolf_module_setup(uint32 version) {
    static constexpr auto api=nolf::client_module_api(CreateClientShell,DeleteClientShell,input,nolf::platform::set_log_sink);
    return version == api.header.version ? &api.header : nullptr;
}
