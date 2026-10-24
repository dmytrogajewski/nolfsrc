#pragma once
#include "sound_cache.h"
#include "sound_request.h"
#include "object_pool.h"
#include "ltlink.h"

class SoundInstance {
public:
    SoundInstance() { m_Link.Init2(this); }
    SoundInstance(const SoundInstance&) = delete;
    SoundInstance& operator=(const SoundInstance&) = delete;
    LTBOOL Init(class CServerMgr&, PlaySoundInfo*, float, CFileRef*, std::shared_ptr<const SoundTrack>);
    void UpdateTimer(float);
    LTObject* GetObject() { return (m_PlaySoundFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL)) ? m_pObject : nullptr; }
    void Term();
    void AddHold() {
        if ((m_PlaySoundFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_TIME | PLAYSOUND_TIMESYNC)) && !(m_PlaySoundFlags & PLAYSOUND_LOOP))
            ++m_ListenerCount;
    }
    void DropListener(uint8* flags) {
        if ((m_PlaySoundFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_TIME | PLAYSOUND_TIMESYNC)) && !(m_PlaySoundFlags & PLAYSOUND_LOOP)) {
            *flags |= 1;
            if (m_ListenerCount) --m_ListenerCount;
            if (!m_ListenerCount) m_fTimer = 0;
        }
    }
    LTLink m_Link;
    CObjectIDRec* m_pUnknown0c = nullptr;
    LTBOOL m_bKill = LTFALSE;
    CFileRef* m_hFile = nullptr;
    std::shared_ptr<const SoundTrack> m_Track;
    float m_fStartTime = 0, m_fTimer = 0, m_fDuration = 0;
    uint32 m_ListenerCount = 0;
    uint16 m_Unknown2c = 0;
    SoundInstance* m_pNextChanged = nullptr;
    uint32 m_PlaySoundFlags = 0;
    CServerMgr* m_Manager = nullptr;
    LTObject* m_pObject = nullptr;
    uint8 m_nPriority = 0;
    float m_fOuterRadius = 0, m_fInnerRadius = 0;
    uint8 m_nVolume = 100;
    float m_fPitchShift = 1;
    LTVector m_vPosition{0, 0, 0};
    uint32 m_UserData = 0;
};
