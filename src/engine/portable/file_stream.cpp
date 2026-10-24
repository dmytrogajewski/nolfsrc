#include "file_stream.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace nolf {
FileStream::FileStream(std::filesystem::path path, platform::FileAccess access) : file_(std::move(path),access), access_(access) {
    if (file_.size() > std::numeric_limits<uint32>::max()) throw std::length_error("file exceeds SDK stream extent");
}
void FileStream::Release() { delete this; }
LTRESULT FileStream::ErrorStatus() { return error_; }
LTRESULT FileStream::Read(void* output, uint32 size) {
    if (!size) return LT_OK;
    if (!output) return error_ = LT_INVALIDPARAMS;
    try {
        file_.read_at(position_,{static_cast<std::byte*>(output),size});
        position_ += size; return LT_OK;
    } catch (const std::exception&) {
        std::memset(output,0,size); return error_ = LT_ERROR;
    }
}
LTRESULT FileStream::Write(const void* input, uint32 size) {
    if (access_ == platform::FileAccess::Read) return error_ = LT_ERROR;
    if (!size) return LT_OK;
    if (!input) return error_ = LT_INVALIDPARAMS;
    if (size > std::numeric_limits<uint32>::max() - position_) return error_ = LT_ERROR;
    try {
        file_.write_at(position_,{static_cast<const std::byte*>(input),size});
        position_ += size; return LT_OK;
    } catch (const std::exception&) { return error_ = LT_ERROR; }
}
LTRESULT FileStream::SeekTo(uint32 position) {
    if (access_ == platform::FileAccess::Read && position > file_.size()) return error_ = LT_ERROR;
    position_ = position; return LT_OK;
}
LTRESULT FileStream::GetPos(uint32* position) {
    if (!position) return error_ = LT_INVALIDPARAMS;
    *position = position_; return LT_OK;
}
LTRESULT FileStream::GetLen(uint32* length) {
    if (!length) return error_ = LT_INVALIDPARAMS;
    *length = static_cast<uint32>(file_.size()); return LT_OK;
}
LTRESULT FileStream::Flush() {
    try { file_.flush(); return error_; }
    catch (const std::exception&) { return error_ = LT_ERROR; }
}
FileStream* OpenFileStream(const std::filesystem::path& path, platform::FileAccess access) noexcept {
    try { return new FileStream(path,access); }
    catch (const std::exception&) { return nullptr; }
}
}
