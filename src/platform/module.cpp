#include "module.h"
#include <stdexcept>

namespace nolf::platform {
const ModuleHeader& Module::setup(std::uint32_t minimum_size) const {
    auto setup = reinterpret_cast<ModuleSetup>(symbol("nolf_module_setup"));
    if (!setup) throw std::runtime_error("native module has no setup function");
    const auto* api = setup(module_api_version);
    if (!api || api->version != module_api_version || api->size < sizeof(ModuleHeader) || api->size < minimum_size)
        throw std::runtime_error("incompatible native module API");
    return *api;
}
}
