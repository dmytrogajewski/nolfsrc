#include "sound_cache.h"
#include <cmath>
#include <limits>

std::shared_ptr<const SoundTrack> SoundCache::Load(const CFileRef& file) {
    const auto found = entries_.find(file.m_Name);
    if (found != entries_.end()) return found->second.track;
    auto staged = std::make_shared<SoundTrack>();
    staged->name = file.m_Name;
    staged->file_id = file.m_nFileID;
    staged->wave = nolf::read_wave(files_.locate(file.m_Name));
    const auto duration = double(staged->wave.data_size) / staged->wave.byte_rate + 0.1;
    if (!std::isfinite(duration) || duration > std::numeric_limits<float>::max())
        throw std::runtime_error("sound duration exceeds server range");
    staged->server_duration = static_cast<float>(duration);
    entries_.emplace(file.m_Name, Entry{staged});
    return staged;
}
bool SoundCache::MarkWanted(const CFileRef& file) {
    Load(file);
    entries_.at(file.m_Name).wanted = true;
    return true;
}
void SoundCache::BeginPass() { for (auto& [name, entry] : entries_) entry.wanted = false; }
void SoundCache::Collect() { std::erase_if(entries_, [](const auto& item) { return !item.second.wanted; }); }
std::vector<uint16> SoundCache::WantedFileIDs() const {
    std::vector<uint16> result;
    for (const auto& [name, entry] : entries_) if (entry.wanted) result.push_back(entry.track->file_id);
    return result;
}
