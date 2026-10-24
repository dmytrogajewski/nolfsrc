#include "platform/diagnostics.h"
#include "platform/logging.h"
#include <cstdlib>
#include <string>

namespace nolf::platform {
void assertion_failed(std::string_view expression, std::string_view file, unsigned line) noexcept {
    try {
        log("assertion", std::string(file) + ':' + std::to_string(line) + ": " + std::string(expression));
    } catch (...) {}
    std::abort();
}
}
