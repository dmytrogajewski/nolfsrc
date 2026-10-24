#pragma once
#include "ltbasedefs.h"
#include <memory>
#include <functional>
#include <utility>

struct ServerLifetime {
    uint64 epoch = 0;
    bool alive = true;
    bool retiring = false;
    std::size_t active_calls = 0;
    std::function<void()> when_idle;
};
// Engine calls run on the owner thread. A public deletion request cancels
// the current generation, then releases its owner after the outer call exits.
class ServerActivity {
public:
    explicit ServerActivity(std::shared_ptr<ServerLifetime> state) : state_(std::move(state)) {
        if (state_) ++state_->active_calls;
    }
    ~ServerActivity() {
        if (state_ && --state_->active_calls == 0 && state_->when_idle) {
            auto callback = std::exchange(state_->when_idle, {});
            callback();
        }
    }
    ServerActivity(const ServerActivity&) = delete;
    ServerActivity& operator=(const ServerActivity&) = delete;
    bool available() const { return state_ && state_->alive && !state_->retiring; }
private:
    std::shared_ptr<ServerLifetime> state_;
};
class ServerCallbackGuard {
public:
    explicit ServerCallbackGuard(std::shared_ptr<const ServerLifetime> state)
        : state_(std::move(state)), epoch_(state_->epoch), alive_(state_->alive), retiring_(state_->retiring) {}
    bool unchanged() const { return state_->alive == alive_ && state_->retiring == retiring_ && state_->epoch == epoch_; }
    bool alive() const { return state_->alive; }
private:
    std::shared_ptr<const ServerLifetime> state_;
    uint64 epoch_;
    bool alive_;
    bool retiring_;
};
