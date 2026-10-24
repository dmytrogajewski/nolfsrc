#include "platform/sockets.h"
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <system_error>

namespace nolf::platform {
static void socket_error(const char* operation) { throw std::system_error(errno, std::generic_category(), operation); }
struct UdpSocket::Impl {
    int handle{-1};
    ~Impl() { if (handle >= 0) close(handle); }
};
Endpoint resolve_ipv4(const Endpoint& endpoint) {
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
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces)) socket_error("enumerate local IPv4 interfaces");
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner(interfaces, freeifaddrs);
    for (const auto* entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
            !(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK)) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (!address->sin_addr.s_addr) continue;
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) socket_error("format local IPv4 interface");
        return text;
    }
    return "127.0.0.1";
}
UdpSocket::UdpSocket(const Endpoint& bind_address, bool broadcast) : impl_(std::make_unique<Impl>()) {
    impl_->handle = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (impl_->handle < 0) socket_error("create UDP socket");
    if (broadcast) {
        int yes = 1;
        if (setsockopt(impl_->handle, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) socket_error("enable broadcast");
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(bind_address.port);
    const auto numeric = resolve_ipv4(bind_address);
    if (inet_pton(AF_INET, numeric.address.c_str(), &local.sin_addr) != 1)
        throw std::invalid_argument("invalid UDP bind address");
    if (bind(impl_->handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) socket_error("bind UDP socket");
}
UdpSocket::~UdpSocket() = default;
UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&&) noexcept = default;
std::uint16_t UdpSocket::port() const {
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    if (getsockname(impl_->handle, reinterpret_cast<sockaddr*>(&local), &size) < 0) socket_error("UDP address");
    return ntohs(local.sin_port);
}
void UdpSocket::send(const Endpoint& destination, std::span<const std::byte> bytes) {
    if (bytes.size() > 65507) throw std::invalid_argument("UDP datagram exceeds IPv4 limit");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(destination.port);
    const int status = getaddrinfo(destination.address.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0) throw std::runtime_error(std::string("resolve UDP peer: ") + gai_strerror(status));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
    const auto written = sendto(impl_->handle, bytes.data(), bytes.size(), 0, addresses->ai_addr, addresses->ai_addrlen);
    if (written < 0) socket_error("send UDP datagram");
    if (static_cast<std::size_t>(written) != bytes.size()) throw std::runtime_error("short UDP send");
}
std::optional<Datagram> UdpSocket::receive(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > INT_MAX) throw std::invalid_argument("invalid UDP timeout");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        pollfd descriptor{impl_->handle, POLLIN, 0};
        const int status = poll(&descriptor, 1, static_cast<int>(std::max(remaining.count(), std::int64_t{0})));
        if (status < 0) { if (errno == EINTR) continue; socket_error("poll UDP socket"); }
        if (!status) return std::nullopt;
        if (!(descriptor.revents & POLLIN)) throw std::runtime_error("UDP socket poll failed");
        Datagram packet;
        packet.bytes.resize(65536);
        sockaddr_in source{};
        socklen_t length = sizeof(source);
        const auto count = recvfrom(impl_->handle, packet.bytes.data(), packet.bytes.size(), MSG_DONTWAIT,
            reinterpret_cast<sockaddr*>(&source), &length);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            socket_error("receive UDP datagram");
        }
        packet.bytes.resize(static_cast<std::size_t>(count));
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &source.sin_addr, text, sizeof(text))) socket_error("format UDP peer");
        packet.peer = {text, ntohs(source.sin_port)};
        return packet;
    }
}
struct TcpSocket::Impl {
    int handle=-1;
    bool ready=false;
    ~Impl() { if (handle>=0) close(handle); }
};
TcpSocket::TcpSocket(const Endpoint& endpoint) : impl_(std::make_unique<Impl>()) {
    const auto numeric=resolve_ipv4(endpoint);
    impl_->handle=socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,0);
    if (impl_->handle<0) socket_error("create TCP socket");
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(numeric.port);
    if (inet_pton(AF_INET,numeric.address.c_str(),&address.sin_addr)!=1) throw std::invalid_argument("invalid TCP address");
    if (::connect(impl_->handle,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0) impl_->ready=true;
    else if (errno!=EINPROGRESS && errno!=EINTR) socket_error("connect TCP socket");
}
TcpSocket::~TcpSocket()=default;
TcpSocket::TcpSocket(TcpSocket&&) noexcept=default;
TcpSocket& TcpSocket::operator=(TcpSocket&&) noexcept=default;
bool TcpSocket::connected() {
    if (impl_->ready) return true;
    pollfd descriptor{impl_->handle,POLLOUT,0};
    const int status=poll(&descriptor,1,0);
    if (status<0) { if (errno==EINTR) return false; socket_error("poll TCP connection"); }
    if (!status) return false;
    int error=0; socklen_t size=sizeof(error);
    if (getsockopt(impl_->handle,SOL_SOCKET,SO_ERROR,&error,&size)<0) socket_error("get TCP connection status");
    if (error) throw std::system_error(error,std::generic_category(),"connect TCP socket");
    impl_->ready=true; return true;
}
std::size_t TcpSocket::send(std::span<const std::byte> bytes) {
    if (bytes.empty() || !connected()) return 0;
    for (;;) {
        const auto count=::send(impl_->handle,bytes.data(),bytes.size(),MSG_NOSIGNAL);
        if (count>=0) return static_cast<std::size_t>(count);
        if (errno==EINTR) continue;
        if (errno==EAGAIN || errno==EWOULDBLOCK) return 0;
        socket_error("send TCP bytes");
    }
}
std::optional<std::size_t> TcpSocket::receive(std::span<std::byte> bytes) {
    if (bytes.empty()) throw std::invalid_argument("empty TCP receive buffer");
    if (!connected()) return std::nullopt;
    for (;;) {
        const auto count=::recv(impl_->handle,bytes.data(),bytes.size(),0);
        if (count>=0) return static_cast<std::size_t>(count);
        if (errno==EINTR) continue;
        if (errno==EAGAIN || errno==EWOULDBLOCK) return std::nullopt;
        socket_error("receive TCP bytes");
    }
}

}
