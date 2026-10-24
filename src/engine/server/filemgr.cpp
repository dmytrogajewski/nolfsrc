#include "filemgr.h"
#include "engine/portable/allocation.h"
#include "platform/system.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

CResourceTree::CResourceTree(const std::filesystem::path& name)
    : path(std::filesystem::canonical(name)), directory(std::filesystem::is_directory(path)) {
    if (directory) assets.mount_directory(path);
    else assets.mount_archive(path);
}
void CFileMgr::reset(CServerMgr* owner) {
    std::lock_guard lock(mutex_);
    if (dispatching_) throw std::logic_error("resource reset during file notification");
    pending_.clear(); files_.clear(); mounts_.clear(); owner_ = owner; notification_ = nullptr;
    owner_thread_ = {};
}
void CFileMgr::flush() {
    std::lock_guard lock(mutex_);
    if (dispatching_) throw std::logic_error("resource flush during file notification");
    pending_.clear(); files_.clear();
}
void CFileMgr::set_notification(Notification callback) {
    std::lock_guard lock(mutex_);
    notification_ = callback;
    owner_thread_ = std::this_thread::get_id();
}
void CFileMgr::dispatch_notifications() {
    std::lock_guard lock(mutex_);
    if (!notification_ || dispatching_) return;
    if (std::this_thread::get_id() != owner_thread_) throw std::logic_error("file notification on wrong engine thread");
    dispatching_ = true;
    try {
        while (!pending_.empty()) {
            auto* file = pending_.front();
            notification_(owner_, *file);
            pending_.pop_front();
        }
        dispatching_ = false;
    } catch (...) { dispatching_ = false; throw; }
}
CResourceTree* CFileMgr::mount(const std::filesystem::path& path) {
    auto record = std::make_unique<CFileTreeRec>();
    record->m_pTree = std::make_unique<CResourceTree>(path);
    auto* result = record->m_pTree.get();
    std::lock_guard lock(mutex_);
    mounts_.push_back(std::move(record));
    return result;
}
void CFileMgr::mount_game(const std::filesystem::path& data) {
    std::vector<std::unique_ptr<CFileTreeRec>> staged;
    for (const auto& path : nolf::game_resource_paths(data)) {
        auto record = std::make_unique<CFileTreeRec>();
        record->m_pTree = std::make_unique<CResourceTree>(path);
        staged.push_back(std::move(record));
    }
    std::lock_guard lock(mutex_);
    mounts_.reserve(mounts_.size() + staged.size());
    for (auto& mount : staged) mounts_.push_back(std::move(mount));
}
bool CFileMgr::find_locked(std::string_view name, CFileTreeRec** tree, uint32* size) const {
    if (tree) *tree = nullptr;
    if (size) *size = 0;
    const auto key = nolf::platform::asset_key(name);
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        auto& assets = (*it)->m_pTree->assets;
        if (!assets.contains(key)) continue;
        auto source = assets.locate(key);
        if (source.size > std::numeric_limits<uint32>::max()) throw std::runtime_error("asset exceeds SDK file length");
        if (tree) *tree = it->get();
        if (size) *size = static_cast<uint32>(source.size);
        return true;
    }
    return false;
}
bool CFileMgr::find(std::string_view name, CFileTreeRec** tree, uint32* size) const {
    std::lock_guard lock(mutex_);
    return find_locked(name, tree, size);
}
nolf::AssetSource CFileMgr::locate(std::string_view name) const {
    std::lock_guard lock(mutex_);
    CFileTreeRec* record = nullptr;
    if (!find_locked(name, &record, nullptr)) throw std::runtime_error("asset not found: " + std::string(name));
    return record->m_pTree->assets.locate(name);
}
int CFileMgr::identify(std::string_view name, uint16 flags, CFileRef** output) {
    if (output) *output = nullptr;
    const auto key = nolf::platform::asset_key(name);
    std::lock_guard lock(mutex_);
    auto existing = files_.find(key);
    if (existing != files_.end()) {
        if (output) *output = existing->second.get();
        return 1;
    }
    CFileTreeRec* tree = nullptr;
    uint32 size = 0;
    if (!find_locked(key, &tree, &size)) return 0;
    // 0xffff is the protocol's missing-file sentinel, never a valid id.
    if (files_.size() >= 0xffff) throw std::runtime_error("network file identifier space exhausted");
    auto ref = std::make_unique<CFileRef>();
    ref->m_Name = key;
    ref->m_nFileID = static_cast<uint16>(files_.size());
    ref->m_Unknown04 = size;
    ref->m_Flags = flags;
    ref->m_pTreeRec = tree;
    auto* result = ref.get();
    files_.emplace(key, std::move(ref));
    if (output) *output = result;
    pending_.push_back(result);
    if (notification_ && std::this_thread::get_id() == owner_thread_) dispatch_notifications();
    return 2;
}
std::vector<CFileRef*> CFileMgr::files() const {
    std::lock_guard lock(mutex_);
    std::vector<CFileRef*> result;
    result.reserve(files_.size());
    for (const auto& [name, file] : files_) result.push_back(file.get());
    // File announcements use registration order, independently of name sorting.
    std::sort(result.begin(), result.end(), [](auto* a, auto* b) { return a->m_nFileID < b->m_nFileID; });
    return result;
}
std::vector<nolf::AssetEntry> CFileMgr::list(std::string_view directory) const {
    std::lock_guard lock(mutex_);
    std::map<std::string, bool> merged;
    for (const auto& mount : mounts_)
        for (const auto& entry : mount->m_pTree->assets.list(directory)) merged[entry.name] = entry.directory;
    std::vector<nolf::AssetEntry> result;
    for (const auto& [name, directoryEntry] : merged) result.push_back({name, directoryEntry});
    return result;
}
void fs_InitModule() {}
void fs_TermModule() {}
void fs_Init(CFileMgr* manager, CServerMgr* owner) { if (manager) manager->reset(owner); }
void fs_Term(CFileMgr* manager) { if (manager) manager->reset(nullptr); }
void fs_FlushFiles(CFileMgr* manager) { if (manager) manager->flush(); }
int fs_OpenTree(char* name, void** output) {
    if (!output) return -1;
    *output = nullptr;
    if (!name) return -1;
    try { *output = new CResourceTree(name); return 0; }
    catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return -1; }
}
void fs_CloseTree(void* tree) { delete static_cast<CResourceTree*>(tree); }
void fs_AddResources(CFileMgr* manager, char** names, uint32 count, void** output, int* added) {
    if (added) *added = 0;
    if (!manager || !names || !added) return;
    for (uint32 i = 0; i < count; ++i) {
        if (!names[i]) continue;
        try {
            auto* tree = manager->mount(names[i]);
            if (output) output[*added] = tree;
            ++*added;
        } catch (const std::exception& error) { nolf::platform::log("assets", error.what()); }
    }
}
int fs_FindFileInTrees(CFileMgr* manager, char* name, CFileTreeRec** tree, uint32* size) {
    if (tree) *tree = nullptr;
    if (size) *size = 0;
    if (!manager || !name) return 0;
    try { return manager->find(name, tree, size); }
    catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return 0; }
}
int fs_GetFileIdentifier(CFileMgr* manager, char* name, int flags, void** output) {
    if (output) *output = nullptr;
    if (!manager || !name || flags < 0 || flags > 0xffff) return 0;
    try {
        CFileRef* result = nullptr;
        const auto status = manager->identify(name, static_cast<uint16>(flags), &result);
        if (output) *output = result;
        return status;
    } catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return 0; }
}
int se_LoadFile(CFileMgr* manager, char* name, uint16 flags, void** output) {
    return fs_GetFileIdentifier(manager, name, flags, output);
}
char* fs_GetFilename(CFileMgr*, void* handle) {
    static char empty[] = "";
    return handle ? static_cast<CFileRef*>(handle)->m_Name.data() : empty;
}
char* fs_GetFileName(CFileMgr* manager, void* handle) { return fs_GetFilename(manager, handle); }
namespace {
FileEntry* build_list(const std::vector<nolf::AssetEntry>& entries, std::string_view directory) {
    std::string prefix;
    if (!directory.empty() && directory != "." && directory != "./" && directory != ".\\")
        prefix = nolf::platform::asset_key(directory) + '/';
    FileEntry* head = nullptr;
    FileEntry** tail = &head;
    try {
        for (const auto& entry : entries) {
            const auto full = prefix + entry.name;
            const auto size = sizeof(FileEntry) + entry.name.size() + 1 + full.size() + 1;
            if (size > std::numeric_limits<uint32>::max()) throw std::length_error("file entry too long");
            auto* item = static_cast<FileEntry*>(dalloc(static_cast<uint32>(size)));
            if (!item) throw std::bad_alloc();
            item->m_pBaseFilename = reinterpret_cast<char*>(item + 1);
            item->m_pFullFilename = item->m_pBaseFilename + entry.name.size() + 1;
            std::memcpy(item->m_pBaseFilename, entry.name.c_str(), entry.name.size() + 1);
            std::memcpy(item->m_pFullFilename, full.c_str(), full.size() + 1);
            item->m_Type = entry.directory ? TYPE_DIRECTORY : TYPE_FILE;
            item->m_pNext = nullptr;
            *tail = item;
            tail = &item->m_pNext;
        }
    } catch (...) { fs_FreeFileList(head); throw; }
    return head;
}
}
void fs_FreeFileList(FileEntry* entry) {
    while (entry) { auto* next = entry->m_pNext; dfree(entry); entry = next; }
}
FileEntry* fs_GetFileList(CFileMgr* manager, char* directory) {
    if (!manager || !directory) return nullptr;
    try { return build_list(manager->list(directory), directory); }
    catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return nullptr; }
}
FileEntry* fs_GetAssetFileList(const nolf::Assets& assets,const char* directory) {
    if (!directory) return nullptr;
    return build_list(assets.list(directory),directory);
}
FileEntry* fs_BuildFileList(void** trees, int count, char* directory) {
    if (!trees || count < 0 || !directory) return nullptr;
    try {
        std::map<std::string, bool> merged;
        for (int i = 0; i < count; ++i) if (trees[i])
            for (const auto& entry : static_cast<CResourceTree*>(trees[i])->assets.list(directory)) merged[entry.name] = entry.directory;
        std::vector<nolf::AssetEntry> entries;
        for (const auto& [name, isDirectory] : merged) entries.push_back({name, isDirectory});
        return build_list(entries, directory);
    } catch (const std::exception& error) { nolf::platform::log("assets", error.what()); return nullptr; }
}
