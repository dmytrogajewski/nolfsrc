#include "engine/portable/compatibility.h"
#include "engine/portable/binary.h"
#include "engine/portable/checksum.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace nolf {
CompatibilityIdentity read_compatibility_identity(std::span<const std::byte> bytes) {
    if (bytes.size() != 16 || std::string_view(reinterpret_cast<const char*>(bytes.data()),8) != "NLFCMP01")
        throw std::runtime_error("invalid retail compatibility identity; reimport original assets");
    BinaryReader input(bytes.subspan(8));
    CompatibilityIdentity identity{input.integer<std::uint32_t>(), input.integer<std::uint32_t>()};
    if (identity.size < 2 || identity.size > 128 * 1024 * 1024)
        throw std::runtime_error("invalid retail client shell size");
    return identity;
}
std::uint32_t client_shell_compatibility_crc(const Assets& assets) {
    const auto identity = read_compatibility_identity(assets.read("native/compatibility/cshell.dll"));
    if (assets.locate("cshell.dll").size != identity.size)
        throw std::runtime_error("retail client shell identity differs from mounted assets");
    return identity.crc32;
}
std::uint32_t asset_crc32(const AssetSource& source) {
    std::array<std::byte,16384> bytes;
    Crc32 checksum;
    for (std::uint64_t position=0; position<source.size;) {
        const auto count = std::size_t(std::min<std::uint64_t>(bytes.size(),source.size-position));
        source.read_at(position,std::span(bytes).first(count)); position += count;
        checksum.update(std::span(bytes).first(count));
    }
    return checksum.value();
}
}
