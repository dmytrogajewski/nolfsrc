#include "engine/client/client_window.h"
#include "iltclient.h"
#include <stdexcept>

namespace nolf {
platform::Window& client_window(ILTClient& client) {
    void* window=nullptr;
    if (!client.GetEngineHook || client.GetEngineHook(const_cast<char*>("NOLFWindow"),&window) != LT_OK || !window)
        throw std::runtime_error("native client window service is unavailable");
    return *static_cast<platform::Window*>(window);
}
}
