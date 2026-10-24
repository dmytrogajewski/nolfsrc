#pragma once
#include "engine/portable/assets.h"
#include "platform/audio.h"

namespace nolf {
class AssetMediaSource final : public platform::MediaSource {
    AssetSource source_;
public:
    explicit AssetMediaSource(AssetSource source) : source_(std::move(source)) {}
    std::uint64_t size() const override { return source_.size; }
    void read(std::uint64_t offset,std::span<std::byte> output) const override { source_.read_at(offset,output); }
};
}
