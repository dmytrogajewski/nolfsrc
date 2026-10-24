#include "platform/termination.h"
#include <csignal>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <cerrno>

namespace nolf::platform {
namespace {
std::mutex handler_mutex;
bool installed = false;
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
}
struct TerminationRequest::Impl {
    struct sigaction interrupt{}, terminate{};
    Impl() {
        std::lock_guard lock(handler_mutex);
        if (installed) throw std::logic_error("termination handler already installed");
        struct sigaction handler{};
        handler.sa_handler = request_stop;
        sigemptyset(&handler.sa_mask);
        stop_requested = 0;
        if (sigaction(SIGINT,&handler,&interrupt) != 0)
            throw std::system_error(errno,std::generic_category(),"install SIGINT handler");
        if (sigaction(SIGTERM,&handler,&terminate) != 0) {
            const auto error = errno;
            sigaction(SIGINT,&interrupt,nullptr);
            throw std::system_error(error,std::generic_category(),"install SIGTERM handler");
        }
        installed = true;
    }
    ~Impl() {
        std::lock_guard lock(handler_mutex);
        sigaction(SIGINT,&interrupt,nullptr);
        sigaction(SIGTERM,&terminate,nullptr);
        installed = false;
    }
};
TerminationRequest::TerminationRequest() : impl_(std::make_unique<Impl>()) {}
TerminationRequest::~TerminationRequest() = default;
bool TerminationRequest::requested() const noexcept { return stop_requested != 0; }
}
