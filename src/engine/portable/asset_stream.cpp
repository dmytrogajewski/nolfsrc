#include "asset_stream.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace nolf {
AssetStream::AssetStream(AssetSource source) : source_(std::move(source)) {
    if (!source_.file || source_.size > std::numeric_limits<uint32>::max() ||
        source_.offset > source_.file->size() || source_.size > source_.file->size() - source_.offset)
        throw std::runtime_error("invalid SDK asset stream extent");
}
void AssetStream::Release() { delete this; }
LTRESULT AssetStream::Read(void* output, uint32 size) {
    if (!size) return LT_OK;
    if (!output) return error_ = LT_INVALIDPARAMS;
    if (size <= source_.size - position_) {
        try {
            constexpr uint32 cache_capacity = 64 * 1024;
            if (size >= cache_capacity) {
                source_.read_at(position_, {static_cast<std::byte*>(output), size});
            } else {
                auto cursor = position_;
                auto* destination = static_cast<std::byte*>(output);
                auto left = size;
                while (left) {
                    if (cursor < cache_position_ || cursor - cache_position_ >= cache_size_) {
                        if (cache_.empty()) cache_.resize(cache_capacity);
                        cache_size_ = 0;
                        cache_position_ = cursor;
                        auto available = static_cast<uint32>(std::min<std::uint64_t>(source_.size - cursor, cache_capacity));
                        source_.read_at(cursor, std::span(cache_).first(available));
                        cache_size_ = available;
                    }
                    auto offset = cursor - cache_position_;
                    auto count = std::min(left, cache_size_ - offset);
                    std::memcpy(destination, cache_.data() + offset, count);
                    cursor += count; destination += count; left -= count;
                }
            }
            position_ += size;
            return LT_OK;
        } catch (const std::exception&) {}
    }
    std::memset(output, 0, size);
    return error_ = LT_ERROR;
}
LTRESULT AssetStream::Write(const void*, uint32) { return error_ = LT_ERROR; }
LTRESULT AssetStream::SeekTo(uint32 position) {
    if (position > source_.size) return error_ = LT_ERROR;
    position_ = position;
    return LT_OK;
}
LTRESULT AssetStream::GetPos(uint32* position) {
    if (!position) return error_ = LT_INVALIDPARAMS;
    *position = position_; return LT_OK;
}
LTRESULT AssetStream::GetLen(uint32* length) {
    if (!length) return error_ = LT_INVALIDPARAMS;
    *length = static_cast<uint32>(source_.size); return LT_OK;
}
LTRESULT AssetStream::ErrorStatus() { return error_; }
}
