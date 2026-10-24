#include "filemgr.h"
#include "engine/portable/asset_stream.h"
#include "platform/system.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <limits>

ILTStream* fs_OpenStreamInTree(CResourceTree* tree, char* filename, int) {
    if (!tree || !filename) return nullptr;
    try { return new nolf::AssetStream(tree->assets.locate(filename)); }
    catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return nullptr; }
}
ILTStream* fs_OpenFileEx(CFileMgr* manager, char* name, int registerFile, int flags) {
    if (!manager || !name) return nullptr;
    ILTStream* stream = nullptr;
    try {
        stream = new nolf::AssetStream(manager->locate(name));
        if (registerFile && !fs_GetFileIdentifier(manager, name, flags, nullptr)) {
            stream->Release(); return nullptr;
        }
        return stream;
    } catch (const std::exception& error) {
        if (stream) stream->Release();
        nolf::platform::log("assets", error.what()); return nullptr;
    }
}
ILTStream* fs_OpenFile(CFileMgr* manager, char* name) { return fs_OpenFileEx(manager, name, 0, 0); }
ILTStream* se_OpenStream(CFileMgr* manager, void* file) {
    return file ? fs_OpenFile(manager, static_cast<CFileRef*>(file)->m_Name.data()) : nullptr;
}
int fs_CopyStreamToFile(ILTStream* input, const char* path) {
    if (!input || !path || input->ErrorStatus() != LT_OK) return 0;
    try {
        uint32 length = 0;
        if (input->GetLen(&length) != LT_OK || input->SeekTo(0) != LT_OK) return 0;
        nolf::platform::AtomicOutputFile output(path);
        std::array<std::byte, 65536> buffer;
        for (uint32 position = 0; position < length;) {
            const uint32 size = std::min<uint32>(buffer.size(), length - position);
            if (input->Read(buffer.data(), size) != LT_OK) return 0;
            output.write(std::span(buffer).first(size));
            position += size;
        }
        if (input->ErrorStatus() != LT_OK) return 0;
        output.commit();
        return 1;
    } catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return 0; }
}
LTRESULT fs_CopyFile(CFileMgr* manager, const char* source, const char* destination) {
    if (!manager || !source || !destination) return LT_INVALIDPARAMS;
    auto* input = fs_OpenFile(manager, const_cast<char*>(source));
    if (!input) return LT_NOTFOUND;
    const bool copied = fs_CopyStreamToFile(input, destination) != 0;
    input->Release();
    return copied ? LT_OK : LT_ERROR;
}
int fs_GetRawFileInfo(CResourceTree* tree, char* filename, char* path, uint32 capacity, uint32* offset, uint32* length) {
    if (!tree || !filename || !path || !capacity || !offset || !length) return 0;
    path[0] = 0; *offset = *length = 0;
    try {
        const auto source = tree->assets.locate(filename);
        const auto physical = source.file->path().string();
        if (source.offset > std::numeric_limits<uint32>::max() || source.size > std::numeric_limits<uint32>::max() || physical.size() >= capacity) return 0;
        std::memcpy(path, physical.c_str(), physical.size() + 1);
        *offset = static_cast<uint32>(source.offset); *length = static_cast<uint32>(source.size);
        return 1;
    } catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return 0; }
}
int fs_GetPhysicalPath(CResourceTree* tree, char* filename, char* path, uint32 capacity) {
    if (!tree || !tree->directory) return 0;
    uint32 offset, length;
    return fs_GetRawFileInfo(tree, filename, path, capacity, &offset, &length);
}
