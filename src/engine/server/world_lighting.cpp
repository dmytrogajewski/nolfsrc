#include "world_lighting.h"
#include "iltstream.h"
#include "engine/portable/wire.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <array>
#include <new>
#include <utility>

namespace {
template<class T> bool read(ILTStream& stream, T& value) {
    std::array<std::byte, sizeof(T)> bytes{};
    if (stream.Read(bytes.data(), static_cast<uint32>(bytes.size())) != LT_OK) return false;
    value = nolf::wire::load<T>(bytes.data());
    return true;
}
bool read_name(ILTStream& stream, char (&name)[32]) {
    uint16 size = 0;
    if (!read(stream, size)) return false;
    std::array<char, 256> buffer{};
    uint32 position = 0;
    while (position < size) {
        const auto count = std::min<uint32>(size - position, buffer.size());
        if (stream.Read(buffer.data(), count) != LT_OK) return false;
        for (uint32 i = 0; i < count && position + i < 31; ++i) name[position + i] = buffer[i];
        position += count;
    }
    return true;
}
}
LightAnim* CWorldData::FindLightAnim(const char* name, uint32* index) {
    if (!name) return nullptr;
    for (uint32 i = 0; i < m_Anims.GetSize(); ++i) {
        if (nolf::ascii_compare(m_Anims[i].m_Name, name) == 0) {
            if (index) *index = i;
            return &m_Anims[i];
        }
    }
    return nullptr;
}
void CWorldData::TermArrays() {
    ++generation_;
    m_Anims.Term(); m_Frames.Term(); m_Lights.Term(); m_FrameEntries.Term(); m_Data.Term();
    m_Flags180 &= ~WD_HAS_BASEANIM;
}
void CWorldData::SwapStorage(CWorldData& other) noexcept {
    ++generation_; ++other.generation_;
    m_Anims.Swap(other.m_Anims); m_Lights.Swap(other.m_Lights);
    m_Frames.Swap(other.m_Frames); m_Data.Swap(other.m_Data); m_FrameEntries.Swap(other.m_FrameEntries);
    std::swap(m_Flags180, other.m_Flags180);
}
LTRESULT CWorldData::LoadObjects(ILTStream* stream) {
    if (!stream) return LT_INVALIDPARAMS;
    try {
        uint32 length = 0, position = 0;
        uint32 lights = 0, frames = 0, data = 0, entries = 0, anims = 0;
        if (stream->GetLen(&length) != LT_OK || m_nObjectsOffset > length ||
            stream->SeekTo(m_nObjectsOffset) != LT_OK ||
            !read(*stream, lights) || !read(*stream, frames) || !read(*stream, data) ||
            !read(*stream, entries) || !read(*stream, anims) ||
            stream->GetPos(&position) != LT_OK || position > length) return LT_ERROR;
        const uint64 remaining = length - position;
        const uint64 disk_minimum = uint64(lights) * 4 + uint64(entries) * 4 + uint64(anims) * 14 + data;
        const uint64 allocation = uint64(anims) * sizeof(LightAnim) + uint64(lights) * sizeof(CLALight) +
            uint64(frames) * sizeof(CLAFrameEntry*) + data + uint64(entries) * sizeof(CLAFrameEntry);
        if (disk_minimum > remaining || allocation > 256ull * 1024 * 1024) return LT_ERROR;
        CWorldData next;
        next.m_Flags180 = m_Flags180 & ~WD_HAS_BASEANIM;
        if (!next.m_Anims.SetSize(anims) || !next.m_Lights.SetSize(lights) || !next.m_Frames.SetSize(frames) ||
            !next.m_Data.SetSize(data) || !next.m_FrameEntries.SetSize(entries)) return LT_OUTOFMEMORY;
        uint32 i_frame = 0, i_light = 0, i_entry = 0, i_data = 0;
        for (uint32 i = 0; i < anims; ++i) {
            auto& anim = next.m_Anims[i];
            if (!read_name(*stream, anim.m_Name) || !read(*stream, anim.m_bShadowMap) ||
                !read(*stream, anim.m_nFrames) || !read(*stream, anim.m_nLights) ||
                anim.m_nFrames > frames - i_frame || anim.m_nLights > lights - i_light ||
                uint64(anim.m_nFrames) * anim.m_nLights > entries - i_entry) return LT_ERROR;
            anim.m_ppFrames = anim.m_nFrames ? next.m_Frames.GetArray() + i_frame : nullptr;
            anim.m_pLights = anim.m_nLights ? next.m_Lights.GetArray() + i_light : nullptr;
            i_frame += anim.m_nFrames; i_light += anim.m_nLights;
            for (uint32 light = 0; light < anim.m_nLights; ++light)
                if (!read(*stream, anim.m_pLights[light].m_n00) || !read(*stream, anim.m_pLights[light].m_n02)) return LT_ERROR;
            for (uint32 frame = 0; frame < anim.m_nFrames; ++frame) {
                auto* row = anim.m_nLights ? next.m_FrameEntries.GetArray() + i_entry : nullptr;
                anim.m_ppFrames[frame] = row;
                i_entry += anim.m_nLights;
                for (uint32 light = 0; light < anim.m_nLights; ++light) {
                    auto& entry = row[light];
                    if (!read(*stream, entry.m_nSize) || entry.m_nSize > data - i_data) return LT_ERROR;
                    entry.m_pData = entry.m_nSize ? next.m_Data.GetArray() + i_data : nullptr;
                    if (entry.m_nSize && stream->Read(entry.m_pData, entry.m_nSize) != LT_OK) return LT_ERROR;
                    i_data += entry.m_nSize;
                }
            }
        }
        if (stream->ErrorStatus() != LT_OK) return LT_ERROR;
        if (auto* base = next.FindLightAnim("LightAnim_BASE")) {
            next.m_Flags180 |= WD_HAS_BASEANIM;
            base->m_iFrames[0] = base->m_iFrames[1] = 0;
            base->m_fPercentBetween = 0;
        }
        SwapStorage(next);
        return LT_OK;
    } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
}
