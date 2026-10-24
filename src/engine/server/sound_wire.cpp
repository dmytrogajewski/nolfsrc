#include "sound_wire.h"
#include "ltobject_internal.h"
#include "engine/portable/wire.h"
#include <array>
#include <limits>

void lmsg_CompressPos(uint8*, LTVector*, CPosRef*);

namespace {
class Record {
public:
    template<class T> void put(T value) {
        nolf::wire::store(data.data() + size,value); size += sizeof(T);
    }
    bool position(LTVector value, CPosRef* reference) {
        if (!reference) return false;
        for (unsigned i = 0; i < 3; ++i) {
            const auto scaled = (value[i] - reference->m_vMin[i]) * reference->m_vScale[i] * 65536.f;
            if (!std::isfinite(scaled) || double(scaled) < INT32_MIN || double(scaled) > INT32_MAX) return false;
        }
        uint8 compressed[7]; lmsg_CompressPos(compressed,&value,reference);
        for (unsigned i = 0; i < 6; ++i) put(compressed[i]);
        return true;
    }
    bool append(ServerMessage& packet) const {
        return !(packet.m_Flags & PACKETFLAG_WRITEOVERFLOW) && packet.WriteData(data.data(),static_cast<uint32>(size));
    }
private:
    // Largest record: header 5, file 2, state 7, volume 1, pitch 4,
    // extended time 5, user data 4, compressed position 6 = 34 bytes.
    std::array<uint8,64> data{};
    std::size_t size = 0;
};
CSoundSentState next_state(uint16 flags, uint8 priority, float outer, float inner, const CSoundSentState* stored) {
    CSoundSentState next;
    const auto [range, ratio] = sound_QuantizeRange(outer,inner);
    next.m_Flags = flags; next.m_Unknown04 = priority; next.m_nRange = range; next.m_nInnerRatio = ratio;
    if (!stored) return next;
    auto state = *stored; snd_MergeSentState(&state,&next); return state;
}
void metadata(Record& output, CSoundSentState& state, uint16 flags) {
    output.put(state.m_ChangeFlags);
    if (state.m_ChangeFlags & SNDSENT_FLAGS) { output.put(flags); state.m_ChangeFlags &= ~SNDSENT_FLAGS; }
    if (state.m_ChangeFlags & SNDSENT_BYTE04) { output.put(state.m_Unknown04); state.m_ChangeFlags &= ~SNDSENT_BYTE04; }
    if ((flags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D)) && (state.m_ChangeFlags & SNDSENT_RANGE)) {
        output.put(state.m_nRange); output.put(state.m_nInnerRatio); state.m_ChangeFlags &= ~SNDSENT_RANGE;
    }
}
}
LTRESULT sound_WriteQueued(const SoundRecord& sound, Client& client, ServerMessage& packet, CPosRef* reference) {
    if (sound.file_name.empty()) return LT_INVALIDPARAMS;
    auto flags = static_cast<uint16>(sound.m_Info.m_dwFlags);
    const bool own_object = (flags & PLAYSOUND_CLIENTLOCAL) && sound.m_Info.m_hObject && sound.m_Info.m_hObject == client.m_pUnknown3AC;
    if ((flags & PLAYSOUND_CLIENTLOCAL) && !own_object) flags = (flags & ~PLAYSOUND_CLIENTLOCAL) | PLAYSOUND_3D;
    const bool spatial = (flags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D)) != 0;
    const auto& listener = client.m_pUnknown3AC ? client.m_pUnknown3AC->m_Pos : client.m_Position;
    if (spatial && sound.m_Info.m_vPosition.DistSqr(listener) > sound.m_Info.m_fOuterRadius * sound.m_Info.m_fOuterRadius * 4.f) return LT_OK;
    auto* stored = snd_FindSentState(&client,sound.file_id);
    auto state = next_state(flags,sound.m_Info.m_nPriority,sound.m_Info.m_fOuterRadius,sound.m_Info.m_fInnerRadius,stored);
    Record output;
    output.put(uint8(0)); output.put(uint8(0)); output.put(sound.file_id);
    metadata(output,state,flags);
    if (flags & PLAYSOUND_CTRL_VOL) output.put(sound.m_Info.m_nVolume);
    if (flags & PLAYSOUND_CTRL_PITCH) output.put(sound.m_Info.m_fPitchShift);
    if (!own_object && spatial && !output.position(sound.m_Info.m_vPosition,reference)) return LT_INVALIDPARAMS;
    output.put(sound.m_Info.m_UserData);
    if (!output.append(packet)) return LT_OUTOFMEMORY;
    if (stored) *stored = state;
    return LT_OK;
}
LTRESULT sound_WriteInstance(SoundInstance& sound, CObjInfo& info, Client& client, ServerMessage& packet, CPosRef* reference, float time) {
    if (!sound.m_hFile || !sound.m_pUnknown0c || sound.m_pUnknown0c->m_nIndex > UINT16_MAX) return LT_INVALIDPARAMS;
    auto flags = static_cast<uint16>(sound.m_PlaySoundFlags);
    auto changes = info.m_ChangeFlags;
    auto position = sound.m_vPosition;
    if (flags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL)) {
        auto* object = sound.GetObject();
        if (object && object == client.m_pUnknown3AC) { flags |= PLAYSOUND_CLIENTLOCAL; changes &= ~2; }
        else {
            if (flags & PLAYSOUND_CLIENTLOCAL) { if (object) position = object->m_Pos; changes |= 2; }
            flags = (flags & ~PLAYSOUND_CLIENTLOCAL) | PLAYSOUND_3D;
        }
    }
    const bool add_hold = (changes & 1) && (sound.m_PlaySoundFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_TIME | PLAYSOUND_TIMESYNC)) && !(sound.m_PlaySoundFlags & PLAYSOUND_LOOP);
    if (add_hold && sound.m_ListenerCount == UINT32_MAX) return LT_ERROR;
    Record output;
    output.put(uint8(0)); output.put(uint8(1)); output.put(static_cast<uint8>(changes)); output.put(static_cast<uint16>(sound.m_pUnknown0c->m_nIndex));
    CSoundSentState* stored = nullptr;
    CSoundSentState state;
    if (changes & 1) {
        output.put(sound.m_hFile->m_nFileID);
        stored = snd_FindSentState(&client,sound.m_hFile->m_nFileID);
        state = next_state(flags,sound.m_nPriority,sound.m_fOuterRadius,sound.m_fInnerRadius,stored);
        metadata(output,state,flags);
        if (flags & PLAYSOUND_CTRL_VOL) output.put(sound.m_nVolume);
        if (flags & PLAYSOUND_CTRL_PITCH) output.put(sound.m_fPitchShift);
        if (flags & PLAYSOUND_TIMESYNC) {
            const auto ms = sound_ElapsedMilliseconds(time,sound.m_fStartTime);
            output.put(static_cast<uint8>(std::min<uint16>(ms,255)));
            if (ms >= 255) output.put(static_cast<uint32>(ms));
        }
        output.put(sound.m_UserData);
    }
    if ((changes & 2) && !output.position(position,reference)) return LT_INVALIDPARAMS;
    if (!output.append(packet)) return LT_OUTOFMEMORY;
    if (stored) *stored = state;
    if (add_hold) sound.AddHold();
    sound.m_vPosition = position;
    info.m_ChangeFlags = changes;
    return LT_OK;
}
