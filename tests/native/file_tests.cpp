#include "filemgr.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/binary.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/file_stream.h"
#include "platform/system.h"
#include <array>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace nolf::platform;
namespace {
void require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
template<class F> void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "expected operation to fail");
}
void write(const fs::path& path, std::string_view text) { atomic_write(path, std::as_bytes(std::span(text))); }
std::vector<std::byte> archive(std::string_view text) {
    nolf::BinaryWriter entry;
    entry.integer<uint32>(0); entry.integer<uint32>(139); entry.integer<uint32>(text.size());
    entry.integer<uint32>(0); entry.integer<uint32>(1);
    for (auto c : std::array<char, 4>{'T', 'X', 'T', 0}) entry.integer<uint8>(c);
    entry.integer<uint32>(0);
    for (auto c : std::string_view("SHARED")) entry.integer<uint8>(c);
    entry.integer<uint8>(0); entry.integer<uint8>(0);
    nolf::BinaryWriter result;
    std::array<std::byte, 127> title{};
    std::memcpy(title.data(), "RezMgr", 6);
    result.bytes(title); result.integer<uint32>(1);
    result.integer<uint32>(139 + text.size()); result.integer<uint32>(entry.data().size());
    result.bytes(std::as_bytes(std::span(text))); result.bytes(entry.data());
    return result.data();
}
struct Stream {
    ILTStream* value;
    explicit Stream(ILTStream* stream) : value(stream) { require(value, "asset stream did not open"); }
    ~Stream() { value->Release(); }
    ILTStream* operator->() { return value; }
    std::string read() {
        uint32 length = 0;
        require(value->GetLen(&length) == LT_OK && value->SeekTo(0) == LT_OK, "asset stream size and seek");
        std::string text(length, '\0');
        require(value->Read(text.data(), length) == LT_OK, "asset stream contents");
        return text;
    }
};
std::vector<std::string> announcements;
void notify(CServerMgr*, const CFileRef& file) { announcements.push_back(file.m_Name); }
class FailingStream final : public CBaseStream {
public:
    void Release() override { delete this; }
    LTRESULT Read(void* data, uint32 count) override {
        if (position != 0) return LT_ERROR;
        std::memset(data, 1, count); position += count; return LT_OK;
    }
    LTRESULT Write(const void*, uint32) override { return LT_ERROR; }
    LTRESULT SeekTo(uint32 offset) override { position = offset; return LT_OK; }
    LTRESULT GetPos(uint32* offset) override { *offset = position; return LT_OK; }
    LTRESULT GetLen(uint32* size) override { *size = 100000; return LT_OK; }
    LTRESULT ErrorStatus() override { return LT_OK; }
private:
    uint32 position = 0;
};
void check_real_data(const fs::path& data) {
    CFileMgr manager;
    manager.mount_game(data);
    nolf::Assets assets; assets.mount_game(data);
    std::string world, model, music;
    for (const auto& name : assets.names()) {
        if (world.empty() && name.starts_with("worlds/") && name.ends_with(".dat")) world = name;
        if (model.empty() && name.ends_with(".abc")) model = name;
        if (music.empty() && name.ends_with(".wav")) music = name;
    }
    for (auto name : {world, model, music}) {
        require(!name.empty(), "missing real game resource class");
        CFileRef* identifier = nullptr;
        require(manager.identify(name, 0, &identifier) == 2 && identifier, "real game file identifier");
        Stream stream(se_OpenStream(&manager, identifier));
        const auto expected = assets.read(name);
        uint32 size = 0; stream->GetLen(&size);
        require(size == expected.size(), "real game resource extent");
        std::vector<std::byte> bytes(size);
        require(stream->Read(bytes.data(), size) == LT_OK && bytes == expected, "real game stream matches archive bytes");
    }
    std::cout << "native engine streams read real world, model, and sound resources\n";
}
void checks() {
    const auto root = fs::temp_directory_path() / ("nolf-files-" + std::to_string(monotonic_nanoseconds()));
    fs::create_directory(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    const auto saved = root / fs::path(u8"Сохранение-世界.dat");
    const auto contents = std::as_bytes(std::span("abcdefgh",8));
    {
        SeekableFile file(saved,FileAccess::Write);
        file.write_at(0,contents); file.write_at(2,std::as_bytes(std::span("XY",2)));
        require(file.size() == 8,"native file overwrite preserves length"); file.flush();
        std::array<std::byte,1> byte{};
        rejects([&] { file.read_at(0,byte); });
        rejects([&] { file.write_at(UINT64_MAX,contents); });
    }
    {
        Stream file(nolf::OpenFileStream(saved,FileAccess::ReadWrite));
        require(file.read() == "abXYefgh","SDK file stream opens Unicode host path");
        require(file->SeekTo(2) == LT_OK && file->Write("cd",2) == LT_OK,"SDK file stream switches read to write");
        uint32 position = 0, length = 0;
        require(file->GetLen(&length) == LT_OK && length == 8 && file->GetPos(&position) == LT_OK && position == 4,"SDK file length preserves cursor");
        require(file.read() == "abcdefgh","SDK file stream switches write to read");
        require(file->SeekTo(UINT32_MAX) == LT_OK && file->Write("x",1) != LT_OK,"SDK file write cannot wrap 32-bit cursor");
        require(file->ErrorStatus() != LT_OK,"file stream error remains sticky");
    }
    {
        Stream file(nolf::OpenFileStream(saved,FileAccess::Read));
        require(file->Read(nullptr,0) == LT_OK && file->SeekTo(7) == LT_OK,"zero read and valid file seek");
        std::array<char,4> partial{'x','x','x','x'};
        require(file->Read(partial.data(),partial.size()) != LT_OK && partial == std::array<char,4>{},"short file read clears entire caller buffer");
        require(file->Write("x",1) != LT_OK && file->SeekTo(9) != LT_OK,"read-only file rejects writes and out-of-extent seeks");
        require(file.read() == "abcdefgh" && file->ErrorStatus() != LT_OK,"operations after failure preserve sticky error");
        require(file->GetLen(nullptr) != LT_OK && file->GetPos(nullptr) != LT_OK && file->Read(nullptr,1) != LT_OK,"file stream validates SDK output pointers");
    }
    require(!nolf::OpenFileStream(root / "missing",FileAccess::Read),"file-open failure returns no stream");
    {
        Stream file(nolf::OpenFileStream(saved,FileAccess::Write));
        uint32 length = 99;
        require(file->GetLen(&length) == LT_OK && length == 0,"write mode truncates existing file");
        require(file->Write("trace",5) == LT_OK,"SDK file writes binary output");
    }
    require(read_file(saved).size() == 5,"file release flushes and closes its owner");
    atomic_write(root / "base.rez", archive("archive bytes"));
    write(root / "directory/Shared.txt", "directory patch");
    write(root / "directory/Data/Case.DAT", "case asset");
    fs::create_directories(root / "directory/Data/Empty");
    CFileMgr manager;
    fs_Init(&manager, nullptr);
    manager.set_notification(notify);
    auto* tree = manager.mount(root / "base.rez");
    Stream archiveStream(fs_OpenFile(&manager, const_cast<char*>("SHARED.TXT")));
    require(archiveStream.read() == "archive bytes", "archive stream reads original payload");
    CFileRef* id = nullptr;
    require(manager.identify("shared.txt", 3, &id) == 2 && id && id->m_nFileID == 0 && id->m_Flags == 3, "file identifier registration");
    CFileRef* same = nullptr;
    require(manager.identify("SHARED.TXT", 0, &same) == 1 && same == id && announcements.size() == 1, "case-normalized stable identifier and single announcement");
    manager.mount(root / "directory");
    Stream patched(se_OpenStream(&manager, id));
    require(patched.read() == "directory patch", "later directory mount overrides archive");
    require(archiveStream.read() == "archive bytes", "open stream retains its original file extent");
    Stream caseStream(fs_OpenFile(&manager, const_cast<char*>("data\\CASE.dat")));
    require(caseStream.read() == "case asset", "Linux case and separator resolution");
    auto* listing = fs_GetFileList(&manager, const_cast<char*>("Data"));
    require(listing && std::string(listing->m_pBaseFilename) == "case.dat" && listing->m_Type == TYPE_FILE, "native-sized SDK file list records");
    require(listing->m_pNext && std::string(listing->m_pNext->m_pFullFilename) == "data/empty" && listing->m_pNext->m_Type == TYPE_DIRECTORY, "empty directory listing");
    fs_FreeFileList(listing);
    {
        nolf::Assets catalogue; catalogue.mount_archive(root/"base.rez"); catalogue.mount_directory(root/"directory");
        listing=fs_GetAssetFileList(catalogue,"DATA");
    }
    require(listing && std::string(listing->m_pFullFilename)=="data/case.dat" && listing->m_Type==TYPE_FILE && listing->m_pNext && std::string(listing->m_pNext->m_pFullFilename)=="data/empty" && listing->m_pNext->m_Type==TYPE_DIRECTORY && !listing->m_pNext->m_pNext,"client asset catalogue did not produce owned SDK file/directory records");
    fs_FreeFileList(listing);
    std::array<char, 1024> physical{};
    uint32 offset = 0, size = 0;
    require(fs_GetRawFileInfo(tree, const_cast<char*>("Shared.txt"), physical.data(), physical.size(), &offset, &size) && offset == 139 && size == 13, "archive raw range metadata");
    require(!fs_GetRawFileInfo(tree, const_cast<char*>("Shared.txt"), physical.data(), 1, &offset, &size) && physical[0] == 0, "raw path buffer cannot truncate silently");
    require(!fs_GetPhysicalPath(tree, const_cast<char*>("Shared.txt"), physical.data(), physical.size()), "archive member is not a standalone file path");
    auto* folder = manager.mount(root / "directory");
    require(fs_GetPhysicalPath(folder, const_cast<char*>("data/case.dat"), physical.data(), physical.size()) && fs::path(physical.data()).filename() == "Case.DAT", "native physical path preserves host case");
    require(fs_OpenFile(&manager, const_cast<char*>("../escape")) == nullptr, "asset traversal rejected by engine API");
    const auto copy = root / "copy.dat";
    write(copy, "old destination");
    FailingStream failure;
    require(!fs_CopyStreamToFile(&failure, copy.string().c_str()) && read_file(copy).size() == 15, "failed stream copy preserves prior destination");
    require(fs_CopyFile(&manager, "data/case.dat", copy.string().c_str()) == LT_OK && read_file(copy).size() == 10, "complete SDK copy atomically replaces destination");
    std::array<uint8, 4> shortRead{1, 1, 1, 1};
    archiveStream->SeekTo(12);
    require(archiveStream->Read(shortRead.data(), shortRead.size()) != LT_OK && shortRead == std::array<uint8, 4>{}, "asset range cannot escape into archive metadata");
    require(archiveStream->ErrorStatus() != LT_OK && archiveStream->SeekTo(1000000) != LT_OK, "asset stream bounds and sticky errors");
    std::vector<std::future<void>> workers;
    for (unsigned i = 0; i < 8; ++i) workers.push_back(std::async(std::launch::async, [&manager, tree] {
        for (unsigned j = 0; j < 100; ++j) {
            Stream input(fs_OpenFile(&manager, const_cast<char*>("shared.txt")));
            require(input.read() == "directory patch", "concurrent independent asset cursor");
            Stream archiveInput(fs_OpenStreamInTree(tree, const_cast<char*>("shared.txt"), 0));
            require(archiveInput->SeekTo(8) == LT_OK, "shared archive seek");
            char tail[5];
            require(archiveInput->Read(tail, sizeof(tail)) == LT_OK && std::string_view(tail, 5) == "bytes", "concurrent archive file ranges");
        }
    }));
    for (auto& worker : workers) worker.get();
    auto registration = std::async(std::launch::async, [&manager] {
        CFileRef* file = nullptr;
        require(manager.identify("data/case.dat", 0, &file) == 2 && file, "worker file registration");
        rejects([&] { manager.dispatch_notifications(); });
    });
    registration.get();
    require(announcements.size() == 1, "worker never invokes client transfer callbacks");
    manager.dispatch_notifications();
    require(announcements.size() == 2 && announcements[1] == "data/case.dat", "worker announcement delivered on engine thread");
    manager.dispatch_notifications();
    require(announcements.size() == 2, "worker announcement delivered once");
    fs_Term(&manager);
    require(caseStream.read() == "case asset" && patched.read() == "directory patch", "open streams outlive manager and mounts");
    rejects([&] { manager.locate("shared.txt"); });
    // A failed staged game mount leaves existing resources available.
    manager.mount(root / "base.rez");
    write(root / "bad-game/archives.txt", "missing.rez\n");
    rejects([&] { manager.mount_game(root / "bad-game"); });
    Stream original(fs_OpenFile(&manager, const_cast<char*>("shared.txt")));
    require(original.read() == "archive bytes", "failed mount preserves previous namespace");
    std::vector<std::byte> large(200003);
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = std::byte((i * 13 + i / 997) & 255);
    atomic_write(root / "cached/Binary.dat", large);
    nolf::Assets cached; cached.mount_directory(root / "cached");
    nolf::AssetStream buffered(cached.locate("binary.dat"));
    for (auto [offset, count] : {std::pair{0u,13u}, std::pair{65530u,4096u}, std::pair{12u,5u},
        std::pair{130000u,70000u}, std::pair{199999u,4u}, std::pair{100u,64000u}}) {
        std::vector<std::byte> result(count);
        require(buffered.SeekTo(offset) == LT_OK && buffered.Read(result.data(), count) == LT_OK &&
            std::equal(result.begin(), result.end(), large.begin() + offset), "resource read-ahead preserves seeks, cache boundaries, direct reads and final partial blocks");
    }
    std::cout << "native engine file identifiers, streaming, overlay, ownership, and atomic copy checks passed\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--data") check_real_data(argv[2]);
        else checks();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
