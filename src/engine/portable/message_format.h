#pragma once
#include "resources.h"
#include <cstdarg>

namespace nolf {
// Resource identity is independent of the executable module's OS handle.
struct ResourceModule {
    const Resources* resources = nullptr;
    std::string name;
    std::uint32_t language = 1033;
};
// Numbered inserts used by the original game resources (%1!s!, %2!d!).
// Invalid, ambiguous, or oversized formats are rejected before reading args.
std::string format_message(std::string_view format, va_list* arguments);
std::string format_resource(const ResourceModule&, std::uint32_t id, va_list* arguments);
}
