#pragma once
#include "engine/portable/assets.h"
#include <optional>

namespace nolf {
struct WaveLoop {
    std::uint32_t cue = 0, start = 0, length = 0;
};
struct WaveMetadata {
    std::uint16_t format = 0, channels = 0, block_align = 0, bits = 0;
    std::uint32_t sample_rate = 0, byte_rate = 0;
    std::vector<std::byte> format_extra;
    std::uint64_t data_offset = 0, data_size = 0;
    std::optional<std::uint64_t> frames;
    std::optional<WaveLoop> loop;
    std::uint32_t lith_flags = 0, lith_metadata = 0;
    double duration() const;
};
// Reads headers only. File extents and memory fixtures use the same bounded
// RIFF parser; sample data stays in the source for streaming/decoding.
WaveMetadata read_wave(const AssetSource&);
WaveMetadata read_wave(std::span<const std::byte>);
}
