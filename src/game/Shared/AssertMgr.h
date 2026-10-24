#pragma once
#include <atomic>

class CAssertMgr {
public:
    static void Enable();
    static void Disable();
    static void ReportFailed(const char* expression, const char* file, unsigned line);
private:
    static std::atomic_bool enabled_;
};

#ifndef NDEBUG
#define GAME_ASSERT(expression) ((expression) ? static_cast<void>(0) : CAssertMgr::ReportFailed(#expression, __FILE__, __LINE__))
#else
#define GAME_ASSERT(expression) static_cast<void>(0)
#endif
