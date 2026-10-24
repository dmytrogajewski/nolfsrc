#include "engine/portable/memory_stream.h"
#include "ltcodes.h"
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nolf {
MemoryStream::MemoryStream(std::vector<std::byte> data, bool writable, uint32 max_extent)
    : data_(std::move(data)), writable_(writable), max_extent_(max_extent) {
    if (data_.size() > max_extent_) throw std::length_error("SDK stream exceeds allowed extent");
}
void MemoryStream::Release() { delete this; }
LTRESULT MemoryStream::Read(void* destination, uint32 size) {
    if (!size) return LT_OK;
    if (!destination) return error_ = LT_INVALIDPARAMS;
    if (size > data_.size() - position_) {
        std::memset(destination, 0, size);
        return error_ = LT_ERROR;
    }
    std::memmove(destination, data_.data() + position_, size);
    position_ += size;
    return LT_OK;
}
LTRESULT MemoryStream::Write(const void* source, uint32 size) {
    if (!writable_) return error_ = LT_ERROR;
    if (!size) return LT_OK;
    if (!source) return error_ = LT_INVALIDPARAMS;
    if (size > max_extent_ - position_) return error_ = LT_ERROR;
    const auto address = reinterpret_cast<std::uintptr_t>(source);
    const auto base = reinterpret_cast<std::uintptr_t>(data_.data());
    const bool internal = address >= base && address - base < data_.size();
    const auto offset = internal ? address - base : 0;
    if (internal && size > data_.size() - offset) return error_ = LT_INVALIDPARAMS;
    const auto end = position_ + size;
    if (end > data_.size()) data_.resize(end);
    if (internal) source = data_.data() + offset;
    std::memmove(data_.data() + position_, source, size);
    position_ = end;
    return LT_OK;
}
LTRESULT MemoryStream::ErrorStatus() { return error_; }
LTRESULT MemoryStream::SeekTo(uint32 position) {
    if (position > data_.size()) return error_ = LT_ERROR;
    position_ = position;
    return LT_OK;
}
LTRESULT MemoryStream::GetPos(uint32* position) {
    if (!position) return error_ = LT_INVALIDPARAMS;
    *position = position_; return LT_OK;
}
LTRESULT MemoryStream::GetLen(uint32* length) {
    if (!length) return error_ = LT_INVALIDPARAMS;
    *length = static_cast<uint32>(data_.size()); return LT_OK;
}
}
