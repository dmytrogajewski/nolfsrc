#include "font_offset_cache.h"
#include "iltclient.h"
#include "engine/portable/binary.h"
#include "engine/portable/checksum.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace nolf {
namespace {
std::mutex cache_mutex;
constexpr std::string_view magic = "NLFFNT03";
constexpr std::size_t max_entries = 128, max_file_size = 256*1024;
struct Entry { FontOffsetKey key; LITHFONTOFFSETDATA offsets; };
void validate(const FontOffsetKey& key, const LITHFONTOFFSETDATA& offsets) {
    if (key.asset.empty() || key.asset.size() >= LTF_OFFSETS_NAME_SIZE || platform::asset_key(key.asset) != key.asset ||
        !key.width || key.width > 65535 || !key.height || key.height > 65535 || key.pitch > 255 || offsets.pOffsets[0])
        throw std::runtime_error("invalid font offset cache key");
    for (unsigned i=0; i<LTF_NUM_TOTAL_CHARS; ++i) {
        if (offsets.pOffsets[i+1] < offsets.pOffsets[i] || offsets.pOffsets[i+1] > key.width ||
            offsets.pWidths[i] != offsets.pOffsets[i+1]-offsets.pOffsets[i] || offsets.pWidths[i] >= key.width)
            throw std::runtime_error("invalid font offsets");
    }
}
std::vector<Entry> read(const std::filesystem::path& path) {
    platform::InputFile file(path);
    if (file.size() > max_file_size) throw std::runtime_error("font offset cache exceeds budget");
    std::vector<std::byte> bytes(std::size_t(file.size())); file.read_at(0,bytes);
    BinaryReader input(bytes);
    const auto prefix = input.bytes(magic.size());
    if (std::memcmp(prefix.data(),magic.data(),magic.size())) throw std::runtime_error("invalid font offset cache version");
    const auto count = input.integer<std::uint32_t>();
    if (count > max_entries) throw std::runtime_error("font offset cache has too many entries");
    std::vector<Entry> entries;
    for (std::uint32_t i=0; i<count; ++i) {
        Entry entry;
        entry.key.width=input.integer<std::uint32_t>(); entry.key.height=input.integer<std::uint32_t>();
        entry.key.groups=input.integer<std::uint32_t>(); entry.key.pitch=input.integer<std::uint32_t>();
        entry.key.crc32=input.integer<std::uint32_t>();
        const auto size=input.integer<std::uint16_t>();
        if (!size || size >= LTF_OFFSETS_NAME_SIZE) throw std::runtime_error("invalid font cache name");
        const auto name=input.bytes(size);
        entry.key.asset.assign(reinterpret_cast<const char*>(name.data()),name.size());
        for (auto& offset : entry.offsets.pOffsets) offset=input.integer<std::uint32_t>();
        for (auto& width : entry.offsets.pWidths) width=input.integer<std::uint32_t>();
        validate(entry.key,entry.offsets);
        if (std::any_of(entries.begin(),entries.end(),[&](const Entry& old) { return old.key == entry.key; }))
            throw std::runtime_error("duplicate font cache identity");
        entries.push_back(std::move(entry));
    }
    if (input.remaining()) throw std::runtime_error("trailing font cache data");
    return entries;
}
}
std::optional<FontOffsetKey> font_offset_key(ILTClient& client, const char* asset, std::uint32_t width,
        std::uint32_t height, std::uint32_t groups, std::uint32_t pitch) {
    try {
        if (!asset) return {};
        auto name=platform::asset_key(asset);
        if (name.size() >= LTF_OFFSETS_NAME_SIZE) return {};
        ILTStream* raw=nullptr;
        const auto status=client.OpenFile(name.data(),&raw);
        const auto release=[](ILTStream* stream) { if (stream) stream->Release(); };
        const std::unique_ptr<ILTStream,decltype(release)> stream(raw,release);
        if (status != LT_OK || !stream) return {};
        const auto size=stream->GetLen();
        if (!size || size > 64*1024*1024) return {};
        std::array<std::byte,16384> buffer;
        Crc32 crc;
        for (std::uint32_t position=0; position<size;) {
            const auto count=std::min<std::uint32_t>(buffer.size(),size-position);
            if (stream->Read(buffer.data(),count) != LT_OK) return {};
            crc.update(std::span(buffer).first(count)); position+=count;
        }
        return FontOffsetKey{std::move(name),width,height,groups,pitch,crc.value()};
    } catch (const std::exception&) { return {}; }
}
FontOffsetCache::FontOffsetCache() : path_(platform::user_directories().cache/"font-offsets-v3.bin") {}
FontOffsetCache::FontOffsetCache(std::filesystem::path path) : path_(std::move(path)) {}
bool FontOffsetCache::load(const FontOffsetKey& key, LITHFONTOFFSETDATA& output) const noexcept {
    try {
        std::lock_guard lock(cache_mutex);
        for (const auto& entry : read(path_)) if (entry.key == key) {
            // The caller owns its reference name; the cache stores only the
            // measured data and never publishes a partial decoded record.
            std::copy(std::begin(entry.offsets.pOffsets),std::end(entry.offsets.pOffsets),output.pOffsets);
            std::copy(std::begin(entry.offsets.pWidths),std::end(entry.offsets.pWidths),output.pWidths);
            return true;
        }
    } catch (const std::exception&) {}
    return false;
}
bool FontOffsetCache::store(const FontOffsetKey& key, const LITHFONTOFFSETDATA& offsets) const noexcept {
    try {
        validate(key,offsets);
        std::lock_guard lock(cache_mutex);
        std::vector<Entry> entries;
        try { entries=read(path_); } catch (const std::exception&) {}
        std::erase_if(entries,[&](const Entry& entry) { return entry.key == key; });
        if (entries.size() == max_entries) entries.erase(entries.begin());
        entries.push_back({key,offsets});
        BinaryWriter output;
        output.bytes(std::as_bytes(std::span(magic))); output.integer<std::uint32_t>(entries.size());
        for (const auto& entry : entries) {
            output.integer(entry.key.width); output.integer(entry.key.height); output.integer(entry.key.groups);
            output.integer(entry.key.pitch); output.integer(entry.key.crc32);
            output.integer<std::uint16_t>(entry.key.asset.size()); output.bytes(std::as_bytes(std::span(entry.key.asset)));
            for (auto offset : entry.offsets.pOffsets) output.integer(offset);
            for (auto width : entry.offsets.pWidths) output.integer(width);
        }
        platform::atomic_write(path_,output.data());
        return true;
    } catch (const std::exception&) { return false; }
}
}
