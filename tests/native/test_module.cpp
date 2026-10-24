#include "platform/module.h"
#if defined(_WIN32)
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" TEST_EXPORT const nolf::platform::ModuleHeader* nolf_module_setup(std::uint32_t version) {
    static constexpr nolf::platform::ModuleHeader api{nolf::platform::module_api_version, sizeof(nolf::platform::ModuleHeader)};
    return version == api.version ? &api : nullptr;
}
