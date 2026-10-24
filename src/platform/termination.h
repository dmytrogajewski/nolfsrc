#pragma once
#include <memory>

namespace nolf::platform {
// The process host polls this request and performs normal engine shutdown on
// its own thread. The OS callback never enters the engine or writes logs.
class TerminationRequest {
public:
    TerminationRequest();
    ~TerminationRequest();
    TerminationRequest(const TerminationRequest&) = delete;
    TerminationRequest& operator=(const TerminationRequest&) = delete;
    bool requested() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
