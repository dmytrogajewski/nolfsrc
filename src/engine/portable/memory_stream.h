#pragma once
#include "basestream.h"
#include <cstddef>
#include <span>
#include <vector>
#include <limits>

namespace nolf {
// Owns its bytes, including when created from an archive entry. Release() is
// the SDK ownership boundary; the allocation is freed in the same module.
class MemoryStream final : public CBaseStream {
public:
    explicit MemoryStream(std::vector<std::byte> data = {}, bool writable = false,
                          uint32 max_extent = std::numeric_limits<uint32>::max());
    void Release() override;
    LTRESULT Read(void* destination, uint32 size) override;
    LTRESULT Write(const void* source, uint32 size) override;
    LTRESULT ErrorStatus() override;
    LTRESULT SeekTo(uint32 position) override;
    LTRESULT GetPos(uint32* position) override;
    LTRESULT GetLen(uint32* length) override;
    using ILTStream::GetLen;
    using ILTStream::GetPos;
    std::span<const std::byte> data() const { return data_; }
private:
    std::vector<std::byte> data_;
    uint32 position_{};
    LTRESULT error_{};
    bool writable_{};
    uint32 max_extent_;
};
}
