#pragma once
#include "server_files.h"
#include "ltbasedefs.h"

class CBaseDriver;
namespace nolf {
// Owner-thread client connection using the same native transport as the
// server. Packet delivery returns owned bytes before entering game/UI code.
class ClientTransport {
public:
    explicit ClientTransport(LTGUID application);
    ~ClientTransport();
    ClientTransport(const ClientTransport&) = delete;
    ClientTransport& operator=(const ClientTransport&) = delete;
    LTRESULT connect(std::string_view address);
    // Pairs with a server's in-process driver in this process.
    LTRESULT connect_local(CBaseDriver& server);
    bool connected() const;
    void send(std::span<const std::byte> packet, bool reliable = true);
    std::vector<PacketBytes> pump(float host_time);
    void disconnect(std::uint32_t reason = 0);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
