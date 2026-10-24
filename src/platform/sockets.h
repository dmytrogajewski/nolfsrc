#pragma once
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nolf::platform {
struct Endpoint {
    std::string address;
    std::uint16_t port;
    bool operator==(const Endpoint&) const = default;
};
// Resolve once when connecting; packet delivery uses numeric IPv4 addresses.
Endpoint resolve_ipv4(const Endpoint&);
std::array<std::uint8_t, 4> ipv4_bytes(const std::string& numeric_address);
std::string local_ipv4_address();
struct Datagram { Endpoint peer; std::vector<std::byte> bytes; };
class UdpSocket {
public:
    explicit UdpSocket(std::uint16_t port = 0, bool broadcast = false);
    explicit UdpSocket(const Endpoint& bind_address, bool broadcast = false);
    ~UdpSocket();
    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    std::uint16_t port() const;
    void send(const Endpoint& destination, std::span<const std::byte> bytes);
    // Empty optional indicates a timeout, not an empty UDP payload.
    std::optional<Datagram> receive(std::chrono::milliseconds timeout);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Nonblocking IPv4 stream. DNS resolution happens once at construction.
// connected() polls connection completion; send/receive never wait for data.
class TcpSocket {
public:
    explicit TcpSocket(const Endpoint&);
    ~TcpSocket();
    TcpSocket(TcpSocket&&) noexcept;
    TcpSocket& operator=(TcpSocket&&) noexcept;
    TcpSocket(const TcpSocket&)=delete;
    TcpSocket& operator=(const TcpSocket&)=delete;
    bool connected();
    std::size_t send(std::span<const std::byte>); // zero means would block
    std::optional<std::size_t> receive(std::span<std::byte>); // nullopt: would block, zero: EOF
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
