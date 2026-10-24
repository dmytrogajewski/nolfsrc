#include "platform/termination.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace nolf::platform {
namespace {
std::mutex handler_mutex;
bool installed = false;
std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free);
BOOL WINAPI request_stop(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    stop_requested.store(true,std::memory_order_relaxed);
    return TRUE;
}
}
struct TerminationRequest::Impl {
    Impl() {
        std::lock_guard lock(handler_mutex);
        if (installed) throw std::logic_error("termination handler already installed");
        stop_requested.store(false,std::memory_order_relaxed);
        if (!SetConsoleCtrlHandler(request_stop,TRUE))
            throw std::system_error(GetLastError(),std::system_category(),"install console termination handler");
        installed = true;
    }
    ~Impl() {
        std::lock_guard lock(handler_mutex);
        SetConsoleCtrlHandler(request_stop,FALSE);
        installed = false;
    }
};
TerminationRequest::TerminationRequest() : impl_(std::make_unique<Impl>()) {}
TerminationRequest::~TerminationRequest() = default;
bool TerminationRequest::requested() const noexcept { return stop_requested.load(std::memory_order_relaxed); }
}
