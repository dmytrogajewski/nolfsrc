#include "engine/portable/wave.h"
#include "engine/portable/wire.h"
#include <array>
#include <functional>
#include <map>
#include <stdexcept>

namespace nolf {
namespace {
[[noreturn]] void invalid(const char* why) { throw std::runtime_error(std::string("invalid WAVE: ") + why); }
constexpr std::uint32_t code(const char* name) {
    return std::uint32_t(name[0]) | std::uint32_t(name[1]) << 8 |
        std::uint32_t(name[2]) << 16 | std::uint32_t(name[3]) << 24;
}
class Parser {
public:
    Parser(std::uint64_t length, std::function<void(std::uint64_t, std::span<std::byte>)> reader)
        : size(length), read(std::move(reader)) {}
    std::uint64_t size;
    std::function<void(std::uint64_t, std::span<std::byte>)> read;
    WaveMetadata parse() {
        const auto root = bytes<12>(0);
        if (u32(root, 0) != code("RIFF") || u32(root, 8) != code("WAVE")) invalid("expected RIFF/WAVE");
        const std::uint64_t count = u32(root, 4);
        if (count < 4 || count > size - 8) invalid("RIFF extent");
        chunks(12, count + 8, code("WAVE"), 0);
        validate();
        return std::move(result);
    }
private:
    WaveMetadata result;
    bool have_format = false, have_data = false, have_lith = false, have_cue = false;
    std::optional<std::uint32_t> fact;
    std::map<std::uint32_t, std::uint32_t> labels;
    std::size_t chunk_count = 0;
    template<std::size_t N> std::array<std::byte, N> bytes(std::uint64_t offset) {
        if (offset > size || N > size - offset) invalid("truncated header");
        std::array<std::byte, N> output;
        read(offset, output);
        return output;
    }
    template<class B> static std::uint32_t u32(const B& data, std::size_t at) { return wire::load<std::uint32_t>(data.data() + at); }
    template<class B> static std::uint16_t u16(const B& data, std::size_t at) { return wire::load<std::uint16_t>(data.data() + at); }
    void chunks(std::uint64_t pos, std::uint64_t end, std::uint32_t form, unsigned depth) {
        if (depth > 16) invalid("LIST nesting limit");
        while (pos < end) {
            if (end - pos < 8 || ++chunk_count > 65536) invalid("chunk header/limit");
            const auto header = bytes<8>(pos);
            const auto id = u32(header, 0);
            const std::uint64_t count = u32(header, 4);
            pos += 8;
            if (count > end - pos) invalid("chunk exceeds parent");
            chunk(id, pos, count, form, depth);
            pos += count;
            // Some original exporters omit the final odd-byte pad. It may
            // only be omitted at the exact end of this container.
            if ((count & 1) && pos < end) ++pos;
        }
    }
    void chunk(std::uint32_t id, std::uint64_t pos, std::uint64_t count, std::uint32_t form, unsigned depth) {
        if (id == code("LIST")) {
            if (count < 4) invalid("short LIST");
            const auto nested = u32(bytes<4>(pos), 0);
            // Original assets also contain opaque vendor LIST payloads
            // (including a 'lith' form). Only RIFF metadata forms contain
            // chunks this reader interprets.
            if (nested == code("INFO") || nested == code("adtl"))
                chunks(pos + 4, pos + count, nested, depth + 1);
        } else if (id == code("fmt ") && form == code("WAVE")) {
            if (have_format || count < 16 || count == 17) invalid("fmt size/duplicate");
            have_format = true;
            const auto fmt = bytes<16>(pos);
            result.format = u16(fmt, 0); result.channels = u16(fmt, 2);
            result.sample_rate = u32(fmt, 4); result.byte_rate = u32(fmt, 8);
            result.block_align = u16(fmt, 12); result.bits = u16(fmt, 14);
            if (count >= 18) {
                const auto extra = u16(bytes<2>(pos + 16), 0);
                if (extra > count - 18) invalid("fmt extension extent");
                result.format_extra.resize(extra);
                if (extra) read(pos + 18, result.format_extra);
            }
        } else if (id == code("data") && form == code("WAVE")) {
            if (have_data) invalid("duplicate data");
            have_data = true; result.data_offset = pos; result.data_size = count;
        } else if (id == code("fact") && form == code("WAVE")) {
            if (count < 4 || fact) invalid("fact size/duplicate");
            fact = u32(bytes<4>(pos), 0);
        } else if (id == code("cue ") && form == code("WAVE")) {
            if (count < 4 || have_cue) invalid("cue size/duplicate");
            have_cue = true;
            const auto cues = u32(bytes<4>(pos), 0);
            if (cues > (count - 4) / 24) invalid("cue extent");
            if (cues) {
                const auto cue = bytes<24>(pos + 4);
                // LithTech uses the first cue's position, and a matching
                // ADTL label for length. Resolve labels after all chunks.
                if (u32(cue, 8) == code("data")) result.loop = WaveLoop{u32(cue, 0), u32(cue, 4), 0};
            }
        } else if (id == code("ltxt") && form == code("adtl")) {
            if (count < 20) invalid("short ltxt");
            const auto label = bytes<20>(pos);
            if (!labels.emplace(u32(label, 0), u32(label, 4)).second) invalid("duplicate loop label");
        } else if (id == code("lith") && form == code("INFO")) {
            if (count < 8 || have_lith) invalid("lith size/duplicate");
            have_lith = true;
            const auto lith = bytes<8>(pos);
            result.lith_flags = u32(lith, 0); result.lith_metadata = u32(lith, 4);
        }
    }
    void validate() {
        auto& w = result;
        if (!have_format || !have_data || !w.sample_rate || !w.byte_rate || !w.block_align || !w.channels || w.channels > 2)
            invalid("missing/invalid audio format");
        if (w.format == 1) {
            if ((w.bits != 8 && w.bits != 16 && w.bits != 24 && w.bits != 32) ||
                w.block_align != w.channels * (w.bits / 8) ||
                std::uint64_t(w.sample_rate) * w.block_align != w.byte_rate || w.data_size % w.block_align)
                invalid("PCM frame layout");
            w.frames = w.data_size / w.block_align;
        } else if (w.format == 0x11) {
            const auto header = 4u * w.channels;
            if (w.bits != 4 || w.block_align < header || (w.block_align - header) % header) invalid("IMA block layout");
            const auto samples = (w.block_align - header) * 2u / w.channels + 1u;
            if (w.format_extra.size() < 2 || u16(w.format_extra, 0) != samples) invalid("IMA samples per block");
            const auto tail = w.data_size % w.block_align;
            if (tail && (tail < header || (tail - header) % header)) invalid("IMA partial block");
            const auto capacity = (w.data_size / w.block_align) * samples + (tail ? (tail - header) * 2 / w.channels + 1 : 0);
            if (fact && *fact > capacity) invalid("fact exceeds IMA data");
            w.frames = fact ? *fact : capacity;
        } else if (w.format == 0x55) {
            if (fact) w.frames = *fact;
        } else invalid("unsupported codec");
        if (w.loop) {
            const auto label = labels.find(w.loop->cue);
            if (label != labels.end()) w.loop->length = label->second;
            if (w.frames && (w.loop->start > *w.frames || w.loop->length > *w.frames - w.loop->start)) invalid("loop exceeds audio");
        }
    }
};
}
double WaveMetadata::duration() const {
    return frames ? double(*frames) / sample_rate : double(data_size) / byte_rate;
}
WaveMetadata read_wave(const AssetSource& source) {
    return Parser{source.size, [&source](auto pos, auto out) { source.read_at(pos, out); }}.parse();
}
WaveMetadata read_wave(std::span<const std::byte> source) {
    return Parser{source.size(), [source](auto pos, auto out) { std::copy_n(source.data() + pos, out.size(), out.data()); }}.parse();
}
}
