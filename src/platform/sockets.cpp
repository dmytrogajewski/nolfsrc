#include "platform/sockets.h"
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace nolf::platform {
std::array<std::uint8_t, 4> ipv4_bytes(const std::string& address) {
    std::array<std::uint8_t, 4> result{};
    std::string_view remaining(address);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto dot = remaining.find('.');
        const auto part = remaining.substr(0, dot);
        unsigned value = 256;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || error != std::errc{} || end != part.data() + part.size() || value > 255 ||
            ((i == 3) != (dot == std::string_view::npos)))
            throw std::invalid_argument("invalid numeric IPv4 address");
        result[i] = static_cast<std::uint8_t>(value);
        if (dot != std::string_view::npos) remaining.remove_prefix(dot + 1);
    }
    return result;
}
UdpSocket::UdpSocket(std::uint16_t port, bool broadcast) : UdpSocket(Endpoint{"0.0.0.0", port}, broadcast) {}
}
