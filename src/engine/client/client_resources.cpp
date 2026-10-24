#include "engine/client/client_resources.h"
#include "engine/portable/resources.h"
#include "iltclient.h"
#include <stdexcept>
namespace nolf {
const Resources& client_resources(ILTClient& client) {
    void* resources=nullptr;
    if (!client.GetEngineHook || client.GetEngineHook(const_cast<char*>("NOLFResources"),&resources) != LT_OK || !resources)
        throw std::runtime_error("native client resource service is unavailable");
    return *static_cast<const Resources*>(resources);
}
}
