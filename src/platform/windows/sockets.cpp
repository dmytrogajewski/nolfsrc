#include "platform/sockets.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <climits>
#include <algorithm>
#include <stdexcept>

namespace nolf::platform {
static void socket_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": Winsock " + std::to_string(WSAGetLastError()));
}
struct Winsock {
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) socket_error("initialize Winsock"); }
    ~Winsock() { WSACleanup(); }
};
struct UdpSocket::Impl {
    Winsock runtime;
    SOCKET handle{INVALID_SOCKET};
    ~Impl() { if (handle != INVALID_SOCKET) closesocket(handle); }
};
Endpoint resolve_ipv4(const Endpoint& endpoint) {
    Winsock runtime;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* addresses = nullptr;
    const int status = getaddrinfo(endpoint.address.c_str(), nullptr, &hints, &addresses);
    if (status || !addresses) throw std::runtime_error("cannot resolve IPv4 address: " + endpoint.address);
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
    char text[INET_ADDRSTRLEN]{};
    const auto* address = reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
    if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) socket_error("format IPv4 address");
    return {text, endpoint.port};
}
std::string local_ipv4_address() {
    Winsock runtime;
    ULONG size = 16384;
    std::vector<std::byte> buffer;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        buffer.resize(size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        const auto status = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW) continue;
        if (status == ERROR_NO_DATA) return "127.0.0.1";
        if (status != NO_ERROR) throw std::runtime_error("enumerate local IPv4 interfaces: " + std::to_string(status));
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* item = adapter->FirstUnicastAddress; item; item = item->Next) {
                if (!item->Address.lpSockaddr || item->Address.lpSockaddr->sa_family != AF_INET) continue;
                const auto* address = reinterpret_cast<const sockaddr_in*>(item->Address.lpSockaddr);
                if (!address->sin_addr.s_addr) continue;
                char text[INET_ADDRSTRLEN]{};
                if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) socket_error("format local IPv4 interface");
                return text;
            }
        }
        return "127.0.0.1";
    }
    throw std::runtime_error("local IPv4 interfaces kept changing during enumeration");
}
UdpSocket::UdpSocket(const Endpoint& bind_address, bool broadcast) : impl_(std::make_unique<Impl>()) {
    impl_->handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->handle == INVALID_SOCKET) socket_error("create UDP socket");
    u_long nonblocking = 1;
    if (ioctlsocket(impl_->handle, FIONBIO, &nonblocking)) socket_error("set UDP nonblocking mode");
    if (broadcast) {
        int yes = 1;
        if (setsockopt(impl_->handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes))) socket_error("enable broadcast");
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(bind_address.port);
    const auto numeric = resolve_ipv4(bind_address);
    if (inet_pton(AF_INET, numeric.address.c_str(), &local.sin_addr) != 1)
        throw std::invalid_argument("invalid UDP bind address");
    if (bind(impl_->handle, reinterpret_cast<sockaddr*>(&local), sizeof(local))) socket_error("bind UDP socket");
}
UdpSocket::~UdpSocket() = default;
UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&&) noexcept = default;
std::uint16_t UdpSocket::port() const {
    sockaddr_in local{}; int size = sizeof(local);
    if (getsockname(impl_->handle, reinterpret_cast<sockaddr*>(&local), &size)) socket_error("UDP address");
    return ntohs(local.sin_port);
}
void UdpSocket::send(const Endpoint& destination, std::span<const std::byte> bytes) {
    if (bytes.size() > 65507) throw std::invalid_argument("UDP datagram exceeds IPv4 limit");
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(destination.address.c_str(), std::to_string(destination.port).c_str(), &hints, &addresses)) socket_error("resolve UDP peer");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
    if (sendto(impl_->handle, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
               addresses->ai_addr, static_cast<int>(addresses->ai_addrlen)) != static_cast<int>(bytes.size())) socket_error("send UDP datagram");
}
std::optional<Datagram> UdpSocket::receive(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > INT_MAX) throw std::invalid_argument("invalid UDP timeout");
    WSAPOLLFD descriptor{impl_->handle, POLLRDNORM, 0};
    const int status = WSAPoll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (status < 0) socket_error("poll UDP socket");
    if (!status) return std::nullopt;
    if (!(descriptor.revents & POLLRDNORM)) throw std::runtime_error("UDP socket poll failed");
    Datagram packet; packet.bytes.resize(65536);
    sockaddr_in source{}; int length = sizeof(source);
    const int count = recvfrom(impl_->handle, reinterpret_cast<char*>(packet.bytes.data()), static_cast<int>(packet.bytes.size()),
                              0, reinterpret_cast<sockaddr*>(&source), &length);
    if (count < 0) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return std::nullopt;
        socket_error("receive UDP datagram");
    }
    packet.bytes.resize(static_cast<std::size_t>(count));
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &source.sin_addr, text, sizeof(text))) socket_error("format UDP peer");
    packet.peer = {text, ntohs(source.sin_port)};
    return packet;
}
struct TcpSocket::Impl {
    Winsock runtime;
    SOCKET handle=INVALID_SOCKET;
    bool ready=false;
    ~Impl() { if (handle!=INVALID_SOCKET) closesocket(handle); }
};
TcpSocket::TcpSocket(const Endpoint& endpoint) : impl_(std::make_unique<Impl>()) {
    const auto numeric=resolve_ipv4(endpoint);
    impl_->handle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (impl_->handle==INVALID_SOCKET) socket_error("create TCP socket");
    u_long nonblocking=1;
    if (ioctlsocket(impl_->handle,FIONBIO,&nonblocking)) socket_error("set TCP nonblocking mode");
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(numeric.port);
    if (inet_pton(AF_INET,numeric.address.c_str(),&address.sin_addr)!=1) throw std::invalid_argument("invalid TCP address");
    if (::connect(impl_->handle,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0) impl_->ready=true;
    else if (WSAGetLastError()!=WSAEWOULDBLOCK && WSAGetLastError()!=WSAEINPROGRESS) socket_error("connect TCP socket");
}
TcpSocket::~TcpSocket()=default;
TcpSocket::TcpSocket(TcpSocket&&) noexcept=default;
TcpSocket& TcpSocket::operator=(TcpSocket&&) noexcept=default;
bool TcpSocket::connected() {
    if (impl_->ready) return true;
    WSAPOLLFD descriptor{impl_->handle,POLLWRNORM,0};
    const int status=WSAPoll(&descriptor,1,0);
    if (status<0) socket_error("poll TCP connection");
    if (!status) return false;
    int error=0; int size=sizeof(error);
    if (getsockopt(impl_->handle,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)) socket_error("get TCP connection status");
    if (error) throw std::runtime_error("connect TCP socket: Winsock "+std::to_string(error));
    impl_->ready=true; return true;
}
std::size_t TcpSocket::send(std::span<const std::byte> bytes) {
    if (bytes.empty() || !connected()) return 0;
    const int count=::send(impl_->handle,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(std::min<std::size_t>(bytes.size(),INT_MAX)),0);
    if (count>=0) return static_cast<std::size_t>(count);
    if (WSAGetLastError()==WSAEWOULDBLOCK) return 0;
    socket_error("send TCP bytes"); return 0;
}
std::optional<std::size_t> TcpSocket::receive(std::span<std::byte> bytes) {
    if (bytes.empty()) throw std::invalid_argument("empty TCP receive buffer");
    if (!connected()) return std::nullopt;
    const int count=::recv(impl_->handle,reinterpret_cast<char*>(bytes.data()),static_cast<int>(std::min<std::size_t>(bytes.size(),INT_MAX)),0);
    if (count>=0) return static_cast<std::size_t>(count);
    if (WSAGetLastError()==WSAEWOULDBLOCK) return std::nullopt;
    socket_error("receive TCP bytes"); return std::nullopt;
}

}
