#pragma once
#include "ltbasedefs.h"
#include "basestream.h"
#include "platform/filesystem.h"

namespace nolf {
class FileStream final : public CBaseStream {
public:
    FileStream(std::filesystem::path, platform::FileAccess);
    void Release() override;
    LTRESULT Read(void*, uint32) override;
    LTRESULT Write(const void*, uint32) override;
    LTRESULT ErrorStatus() override;
    LTRESULT SeekTo(uint32) override;
    LTRESULT GetPos(uint32*) override;
    LTRESULT GetLen(uint32*) override;
    LTRESULT Flush();
private:
    platform::SeekableFile file_;
    platform::FileAccess access_;
    uint32 position_ = 0;
    LTRESULT error_ = LT_OK;
};
FileStream* OpenFileStream(const std::filesystem::path&, platform::FileAccess) noexcept;
}
