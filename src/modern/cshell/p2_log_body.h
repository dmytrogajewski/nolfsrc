// Compiled into each game module. File, directory and synchronization policy
// belong to the platform logger, including the dedicated-server destination.
#include "platform/logging.h"
#include "cshell/p2_log.h"
#include <cstdarg>
#include <cstdio>

void ModernLog(const char* format, ...) {
    if (!format) return;
    va_list arguments;
    va_start(arguments, format);
    char line[1024];
    const auto length = std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0) return;
    nolf::platform::log("modern", line);
}
