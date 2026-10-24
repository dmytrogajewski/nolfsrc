#include "de_error.h"
#include "platform/system.h"
#include <array>
#include <cstdarg>
#include <cstdio>

int g_DebugLevel = 1;
static char error_format[] = "LT ERROR: %s returned %s (%s)";
char* g_pErrorFormat = error_format;
char g_ShutdownMessage[1024]{};
extern "C" void dsi_OnReturnError(int) {}
void dsi_ServerError(char* format, ...) {
    std::array<char, 4096> message{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message.data(), message.size(), format, arguments);
    va_end(arguments);
    nolf::platform::log("engine", message.data());
}
void con_Printf(char* format, ...) {
    std::array<char, 10000> message{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message.data(), message.size(), format, arguments);
    va_end(arguments);
    nolf::platform::log("console", message.data());
}
void dsi_PrintString(char* message) {
    if (message) nolf::platform::log("console", message);
}
