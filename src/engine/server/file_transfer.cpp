#include "file_transfer.h"
#include "message.h"
#include "net_manager.h"
#include "platform/filesystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <new>

namespace {
constexpr uint32 chunk_size = 0xfd8;
constexpr uint32 packet_size = 0x1000;
constexpr double bytes_per_second = 100000;
}

CClientFiles::CClientFiles(CPacketHolderDesc descriptor, uint32 flags)
    : descriptor_(descriptor), flags_(flags) {}
CClientFiles::~CClientFiles() { Reset(); }

bool CClientFiles::Send(ServerMessage& message) {
    if ((message.m_Flags & PACKETFLAG_WRITEOVERFLOW) || !descriptor_.manager || !descriptor_.connection) return false;
    auto& connections = descriptor_.manager->m_Connections;
    if (connections.FindElement(descriptor_.connection) >= connections.GetSize()) return false;
    return descriptor_.manager->SendToConnection(&message, descriptor_.connection, 0x80);
}
CClientFiles::File* CClientFiles::Find(uint16 id) {
    for (auto& file : files_) if (file.id == id) return &file;
    return nullptr;
}
std::size_t CClientFiles::counted_count() const {
    return std::count_if(files_.begin(),files_.end(),[](const File& file) { return file.counted; });
}
void CClientFiles::Reset() {
    active_ = nullptr;
    remaining_ = 0;
    chunks_ = 0;
    credit_ = 0;
    stream_.reset();
}
void CClientFiles::Cancel() {
    if (!active_) return;
    active_->requested = false;
    ServerMessage message;
    if (message.m_pData) { message.m_pData[0] = 0x34; Send(message); }
    Reset();
}
void CClientFiles::Erase(File* target) {
    if (active_ == target) Cancel();
    for (auto it = files_.begin(); it != files_.end(); ++it) if (&*it == target) {
        files_.erase(it);
        return;
    }
}
bool CClientFiles::Add(const char* name, uint32 size, uint32 id, uint16 flags) {
    if (!name || id > 0x7fff) return false;
    try {
        auto key = nolf::platform::asset_key(name);
        // A file record occupies a word, a dword and a terminated name.
        if (key.empty() || key.size() + 8 > packet_size) return false;
        if (auto* existing = Find(static_cast<uint16>(id)))
            return existing->name == key && existing->size == size;
        File file{std::move(key),size,static_cast<uint16>(id),(flags & 1) != 0,true};
        if (file.counted) files_.push_front(std::move(file));
        else files_.push_back(std::move(file));
        if (!(flags & 2)) Announce();
        return true;
    } catch (const std::exception&) { return false; }
}
bool CClientFiles::Announce() {
    ServerMessage message;
    if (!message.m_pData) return false;
    message.m_pData[0] = 0x32;
    auto first = files_.begin();
    auto end = first;
    bool has_entries = false;
    auto flush = [&] {
        if (!has_entries) return true;
        if (!Send(message)) return false;
        for (auto it = first; it != end; ++it) it->announce = false;
        first = end;
        has_entries = false;
        message.m_DataLen = message.m_Pos = 1;
        return true;
    };
    for (auto it = files_.begin(); it != files_.end(); ++it) if (it->announce) {
        auto& file = *it;
        const auto needed = static_cast<uint32>(file.name.size() + 7);
        if (!message.CanWrite(needed) && !flush()) return false;
        message.WriteWord(file.id);
        message.WriteDWord(file.size);
        message.WriteString(file.name.data());
        end = std::next(it);
        has_entries = true;
    }
    return flush();
}
bool CClientFiles::Handle(ServerMessage& message) {
    if (message.m_Pos < 1 || !message.CanRead(0) || !message.m_DataLen || message.m_DataLen > message.m_Data.GetSize()) return false;
    const auto id = message.m_pData[0] & 0x3f;
    const auto remaining = message.m_DataLen - message.m_Pos;
    if (id == 0x38) {
        if (remaining) return false;
        chunks_ = 0;
        return true;
    }
    // Validate the whole reply before modifying the queue. An odd tail must
    // not leave a partial response applied, or loop on an unchanged cursor.
    if (id != 0x36 || (remaining & 1)) return false;
    while (message.CanRead(2)) {
        const auto word = message.ReadWord(nullptr);
        auto* file = Find(word & 0x7fff);
        if (!file) continue;
        if (word & 0x8000) Erase(file);
        else file->requested = true;
    }
    return true;
}
void CClientFiles::Update(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || (flags_ & 2)) return;
    if (!active_) {
        File* next = nullptr;
        for (auto& file : files_) if (file.requested && (!(flags_ & 1) || file.counted)) { next = &file; break; }
        if (!next) return;
        std::unique_ptr<ILTStream, Release> candidate;
        try { if (descriptor_.open) candidate.reset(descriptor_.open(descriptor_.context,next->name.c_str())); }
        catch (const std::exception&) { Erase(next); return; }
        uint32 length = 0;
        if (!candidate || candidate->GetLen(&length) != LT_OK || length != next->size || candidate->SeekTo(0) != LT_OK) {
            Erase(next);
            return;
        }
        stream_ = std::move(candidate);
        active_ = next;
        remaining_ = next->size;
        chunks_ = 0;
        credit_ = 0;
        return;
    }
    credit_ = std::min(credit_ + double(seconds) * bytes_per_second, double(chunk_size) * 6);
    const auto count = std::min(chunk_size,remaining_);
    if (chunks_ >= 6 || credit_ < count) return;
    ServerMessage message;
    if (!message.m_pData) return;
    message.m_pData[0] = 0x35;
    std::array<uint8,chunk_size> bytes{};
    uint32 position = 0;
    if (stream_->GetPos(&position) != LT_OK || stream_->Read(bytes.data(),count) != LT_OK ||
        stream_->ErrorStatus() != LT_OK || !message.WriteData(bytes.data(),count)) {
        Erase(active_);
        return;
    }
    if (!Send(message)) {
        if (stream_->SeekTo(position) != LT_OK) Erase(active_);
        return;
    }
    remaining_ -= count;
    bytes_sent_ += count;
    credit_ -= count;
    ++chunks_;
    if (!remaining_) {
        auto* completed = active_;
        Reset();
        Erase(completed);
    }
}

CClientFiles* ph_CreateHolder(const CPacketHolderDesc* descriptor, uint32 flags) {
    if (!descriptor || !descriptor->open || !descriptor->manager || !descriptor->connection) return nullptr;
    return new(std::nothrow) CClientFiles(*descriptor,flags);
}
void xfer_Term(CClientFiles* state) { delete state; }
int xfer_AddFile(CClientFiles* state, const char* name, uint32 size, uint32 id, uint16 flags) {
    return state && state->Add(name,size,id,flags);
}
void xfer_SendPendingFiles(CClientFiles* state) { if (state) state->Announce(); }
int xfer_HandleMessage(CClientFiles* state, ServerMessage* message) { return state && message && state->Handle(*message); }
void xfer_Update(CClientFiles* state, float seconds) { if (state) state->Update(seconds); }
