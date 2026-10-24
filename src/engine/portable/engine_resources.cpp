#include "engine_resources.h"
#include "message_format.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <mutex>

namespace {
std::mutex resources_mutex;
std::shared_ptr<const nolf::Resources> catalogue;
std::uint32_t resource_language = 1033;
}
namespace nolf {
void configure_engine_resources(std::shared_ptr<const Resources> resources, std::uint32_t language) {
    // Detect an incomplete import before replacing a working catalogue.
    if (resources) resources->string("ltmsg", 1, language);
    std::lock_guard lock(resources_mutex);
    catalogue = std::move(resources); resource_language = language;
}
bool engine_resources_ready() {
    std::lock_guard lock(resources_mutex);
    return bool(catalogue);
}
}

int dsi_SetupMessage(char* output, int capacity, int code, va_list arguments) {
    if (!output || capacity <= 0) return 0;
    output[0] = 0;
    std::shared_ptr<const nolf::Resources> resources;
    std::uint32_t language;
    {
        std::lock_guard lock(resources_mutex);
        resources = catalogue; language = resource_language;
    }
    std::string message;
    if (resources) {
        if (arguments) {
            va_list copy;
            va_copy(copy, arguments);
            try { message = nolf::format_resource({resources.get(), "ltmsg", language}, std::uint32_t(code), &copy); }
            catch (const std::exception&) {}
            va_end(copy);
        } else {
            try { message = nolf::format_resource({resources.get(), "ltmsg", language}, std::uint32_t(code), nullptr); }
            catch (const std::exception&) {}
        }
    }
    if (message.empty()) message = "Engine error " + std::to_string(code);
    const auto length = std::min<std::size_t>(message.size(), std::size_t(capacity - 1));
    std::memcpy(output, message.data(), length);
    output[length] = 0;
    return static_cast<int>(length);
}
