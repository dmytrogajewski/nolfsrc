#pragma once
#include "ltbasedefs.h"
#include "iltstream.h"
#include <list>
#include <memory>
#include <string>

class CNetMgr;
class NetConnection;
class ServerMessage;

struct CPacketHolderDesc {
    // The transfer owns the returned stream and calls Release exactly once.
    ILTStream* (*open)(void* context, const char* name) = nullptr;
    void* context = nullptr;
    CNetMgr* manager = nullptr;
    NetConnection* connection = nullptr;
};

class CClientFiles {
public:
    explicit CClientFiles(CPacketHolderDesc, uint32 flags);
    ~CClientFiles();
    CClientFiles(const CClientFiles&) = delete;
    CClientFiles& operator=(const CClientFiles&) = delete;
    // Entry flags: 1 = required during initial loading, 2 = batch announcement.
    // Holder flags: 1 = transfer required entries only, 2 = disable transfers.
    bool Add(const char* name, uint32 size, uint32 id, uint16 flags);
    bool Announce();
    bool Handle(ServerMessage&);
    void Update(float seconds);
    void Cancel();
    std::size_t file_count() const { return files_.size(); }
    std::size_t counted_count() const;
    bool transferring() const { return active_ != nullptr; }
    uint64 bytes_sent() const { return bytes_sent_; }
private:
    struct File {
        std::string name;
        uint32 size;
        uint16 id;
        bool counted, announce, requested = false;
    };
    struct Release { void operator()(ILTStream* stream) const { if (stream) stream->Release(); } };
    bool Send(ServerMessage&);
    void Reset();
    void Erase(File*);
    File* Find(uint16);
    CPacketHolderDesc descriptor_;
    uint32 flags_;
    std::list<File> files_;
    std::unique_ptr<ILTStream, Release> stream_;
    File* active_ = nullptr;
    uint32 remaining_ = 0;
    unsigned chunks_ = 0;
    double credit_ = 0;
    uint64 bytes_sent_ = 0;
};

CClientFiles* ph_CreateHolder(const CPacketHolderDesc*, uint32 flags);
void xfer_Term(CClientFiles*);
int xfer_AddFile(CClientFiles*, const char*, uint32 size, uint32 id, uint16 flags);
void xfer_SendPendingFiles(CClientFiles*);
int xfer_HandleMessage(CClientFiles*, ServerMessage*);
void xfer_Update(CClientFiles*, float seconds);
