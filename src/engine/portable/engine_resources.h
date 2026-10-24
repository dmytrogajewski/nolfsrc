#pragma once
#include "resources.h"
#include <memory>
#include <cstdarg>

namespace nolf {
// The host supplies imported resource data. Engine startup never executes a
// resource DLL. A published catalogue retains its owner during formatting.
void configure_engine_resources(std::shared_ptr<const Resources>, std::uint32_t language = 1033);
bool engine_resources_ready();
}
int dsi_SetupMessage(char* output, int capacity, int code, va_list arguments);
