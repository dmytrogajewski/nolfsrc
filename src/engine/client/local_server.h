#pragma once
#include "engine/portable/resources.h"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>

class CBaseDriver;
namespace nolf {
// The original game server running in this process for a single-player or
// locally hosted game, reachable on a loopback port only. One at a time. It
// lives on its own thread, as clients join it over the network transport.
class LocalServer {
public:
    LocalServer(const std::filesystem::path& data,const std::filesystem::path& module,
        std::shared_ptr<const Resources>,std::span<const std::byte> game_info);
    ~LocalServer();
    LocalServer(const LocalServer&)=delete;
    LocalServer& operator=(const LocalServer&)=delete;
    // "127.0.0.1:port" for a network client.
    const std::string& address() const { return address_; }
    // The server's in-process driver; it outlives the server's connections.
    CBaseDriver& driver() const { return *driver_; }
    // Re-raises a failure of the server thread on the caller's.
    void update();
private:
    struct State;
    std::shared_ptr<State> state_;
    std::string address_;
    CBaseDriver* driver_=nullptr;
};
}
