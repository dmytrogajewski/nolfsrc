#pragma once
#include "assets.h"
#include "basestream.h"
#include "ltcodes.h"

namespace nolf {
// A bounded SDK stream owns a share of its physical file, independently of
// the resource mount or file manager that opened it.
class AssetStream final : public CBaseStream {
public:
    explicit AssetStream(AssetSource source);
    void Release() override;
    LTRESULT Read(void*, uint32) override;
    LTRESULT Write(const void*, uint32) override;
    LTRESULT SeekTo(uint32) override;
    LTRESULT GetPos(uint32*) override;
    LTRESULT GetLen(uint32*) override;
    LTRESULT ErrorStatus() override;
private:
    AssetSource source_;
    uint32 position_ = 0;
    LTRESULT error_ = LT_OK;
    // Read-only resources are decoded a scalar at a time by the legacy SDK.
    // Keep read-ahead local to this stream and its independent cursor.
    std::vector<std::byte> cache_;
    uint32 cache_position_ = 0, cache_size_ = 0;
};
}
