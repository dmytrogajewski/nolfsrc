#pragma once
#include "engine/portable/assets.h"
#include <cstdint>
#include <span>

namespace nolf {
// Retail protocol identity retained by the importer, not a checksum of the
// native module and not a claim that executable content has been authenticated.
struct CompatibilityIdentity { std::uint32_t size, crc32; };
CompatibilityIdentity read_compatibility_identity(std::span<const std::byte>);
std::uint32_t client_shell_compatibility_crc(const Assets&);
std::uint32_t asset_crc32(const AssetSource&);
}
