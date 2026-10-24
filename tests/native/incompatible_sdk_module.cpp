#include "engine/portable/game_module.h"

namespace {
int calls = 0;
ClassDef** objects(int*, ILTServer*, int*) { ++calls; return nullptr; }
IServerShell* shell(ILTServer*) { return nullptr; }
void destroy(IServerShell*) {}
constexpr auto descriptor() {
    auto api = nolf::server_module_api(objects, shell, destroy);
    api.sdk_abi ^= 1;
    return api;
}
}
extern "C" NOLF_EXPORT const nolf::platform::ModuleHeader* nolf_module_setup(uint32 version) {
    static constexpr auto api = descriptor();
    return version == api.header.version ? &api.header : nullptr;
}
extern "C" NOLF_EXPORT int test_module_calls() { return calls; }
