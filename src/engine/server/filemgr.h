#pragma once
#include "ltbasedefs.h"
#include "engine/portable/assets.h"
#include <map>
#include <mutex>
#include <deque>
#include <thread>

class CServerMgr;
class CResourceTree {
public:
    explicit CResourceTree(const std::filesystem::path&);
    nolf::Assets assets;
    std::filesystem::path path;
    bool directory;
};
struct CFileTreeRec { std::unique_ptr<CResourceTree> m_pTree; };
struct CFileRef {
    std::string m_Name;
    uint32 m_Unknown04 = 0; // Byte length announced to clients.
    uint16 m_nFileID = 0;
    uint16 m_Flags = 0;
    CFileTreeRec* m_pTreeRec = nullptr;
};
using CFileIdentifier = CFileRef;

// Owns mounts and stable file identifiers. Asset streams own their file
// handles separately and remain valid when this manager is terminated.
class CFileMgr {
public:
    CFileMgr() = default;
    ~CFileMgr() = default;
    CFileMgr(const CFileMgr&) = delete;
    CFileMgr& operator=(const CFileMgr&) = delete;
    using Notification = void (*)(CServerMgr*, const CFileRef&);
    void reset(CServerMgr*);
    CResourceTree* mount(const std::filesystem::path&);
    void mount_game(const std::filesystem::path&);
    bool find(std::string_view, CFileTreeRec**, uint32*) const;
    nolf::AssetSource locate(std::string_view) const;
    int identify(std::string_view, uint16 flags, CFileRef**);
    std::vector<nolf::AssetEntry> list(std::string_view) const;
    std::vector<CFileRef*> files() const;
    void flush();
    void set_notification(Notification callback);
    void dispatch_notifications();
private:
    mutable std::recursive_mutex mutex_;
    std::vector<std::unique_ptr<CFileTreeRec>> mounts_;
    std::map<std::string, std::unique_ptr<CFileRef>> files_;
    CServerMgr* owner_ = nullptr;
    Notification notification_ = nullptr;
    std::thread::id owner_thread_;
    std::deque<CFileRef*> pending_;
    bool dispatching_ = false;
    bool find_locked(std::string_view, CFileTreeRec**, uint32*) const;
};

void fs_InitModule();
void fs_TermModule();
void fs_Init(CFileMgr*, CServerMgr*);
void fs_Term(CFileMgr*);
void fs_FlushFiles(CFileMgr*);
void fs_AddResources(CFileMgr*, char**, uint32, void**, int*);
int fs_OpenTree(char*, void**);
void fs_CloseTree(void*);
int fs_FindFileInTrees(CFileMgr*, char*, CFileTreeRec**, uint32*);
int fs_GetFileIdentifier(CFileMgr*, char*, int, void**);
int se_LoadFile(CFileMgr*, char*, uint16, void**);
ILTStream* fs_OpenFile(CFileMgr*, char*);
ILTStream* fs_OpenFileEx(CFileMgr*, char*, int, int);
ILTStream* se_OpenStream(CFileMgr*, void*);
ILTStream* fs_OpenStreamInTree(CResourceTree*, char*, int);
int fs_CopyStreamToFile(ILTStream*, const char*);
LTRESULT fs_CopyFile(CFileMgr*, const char*, const char*);
FileEntry* fs_GetFileList(CFileMgr*, char*);
FileEntry* fs_GetAssetFileList(const nolf::Assets&,const char*);
FileEntry* fs_BuildFileList(void**, int, char*);
void fs_FreeFileList(FileEntry*);
char* fs_GetFilename(CFileMgr*, void*);
char* fs_GetFileName(CFileMgr*, void*);
int fs_GetRawFileInfo(CResourceTree*, char*, char*, uint32, uint32*, uint32*);
int fs_GetPhysicalPath(CResourceTree*, char*, char*, uint32);
