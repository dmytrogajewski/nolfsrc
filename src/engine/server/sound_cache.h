#pragma once
#include "filemgr.h"
#include "engine/portable/wave.h"
#include <map>

struct SoundTrack {
    std::string name;
    uint16 file_id = 0;
    nolf::WaveMetadata wave;
    // Server completion policy matches the original byte-rate duration and
    // its 100 ms allowance. Decoder duration remains separate in WaveMetadata.
    float server_duration = 0;
};

// The server owns cache membership; callers may retain a track independently
// across collection, cache passes, and file-manager shutdown.
class SoundCache {
public:
    explicit SoundCache(CFileMgr& files) : files_(files) {}
    std::shared_ptr<const SoundTrack> Load(const CFileRef&);
    bool MarkWanted(const CFileRef&);
    void BeginPass();
    void Collect();
    void Clear() { entries_.clear(); }
    std::vector<uint16> WantedFileIDs() const;
    std::size_t Size() const { return entries_.size(); }
private:
    struct Entry { std::shared_ptr<const SoundTrack> track; bool wanted = false; };
    CFileMgr& files_;
    std::map<std::string, Entry> entries_;
};
