#include "servermgr.h"
#include "de_error.h"
#include <algorithm>
#include <cmath>

int sm_ClaimObjectID(CServerMgr*, void**, uint16);

LTRESULT sm_SetSoundTrackChangeFlags(CServerMgr* manager, SoundInstance* sound, uint32 flags) {
    if (!manager || !sound || !sound->m_pUnknown0c) return LT_INVALIDPARAMS;
    const auto index = sound->m_pUnknown0c->m_nIndex;
    if (index >= manager->m_Unknown8fc.GetSize()) return LT_ERROR;
    const auto& entry = manager->m_Unknown8fc[index];
    if (entry.m_nFlags != 2 || entry.m_pRecord != sound) return LT_ERROR;
    if (!sound->m_Unknown2c) {
        sound->m_pNextChanged = manager->m_pChangedSounds;
        manager->m_pChangedSounds = sound;
    }
    sound->m_Unknown2c |= static_cast<uint16>(flags);
    return LT_OK;
}

LTRESULT CServerMgr::GetSoundDuration(HLTSOUND handle, LTFLOAT& duration) {
    auto* sound = reinterpret_cast<SoundInstance*>(handle);
    if (!m_SoundInstances.Contains(sound) || !sound->m_Track) return LT_INVALIDPARAMS;
    duration = sound->m_fDuration;
    return LT_OK;
}
LTRESULT CServerMgr::IsSoundDone(HLTSOUND handle, LTBOOL& done) {
    auto* sound = reinterpret_cast<SoundInstance*>(handle);
    if (!m_SoundInstances.Contains(sound)) return LT_INVALIDPARAMS;
    done = sound->m_Track && sound->m_fTimer <= 0;
    return LT_OK;
}
LTRESULT CServerMgr::KillSound(HLTSOUND handle) {
    auto* sound = reinterpret_cast<SoundInstance*>(handle);
    if (!m_SoundInstances.Contains(sound)) return LT_INVALIDPARAMS;
    if (!(sound->m_PlaySoundFlags & PLAYSOUND_GETHANDLE)) return LT_ERROR;
    sound->m_fTimer = 0;
    sound->m_bKill = LTTRUE;
    return LT_OK;
}
LTRESULT CServerMgr::KillSoundLoop(HLTSOUND handle) {
    auto* sound = reinterpret_cast<SoundInstance*>(handle);
    if (!m_SoundInstances.Contains(sound)) return LT_INVALIDPARAMS;
    if ((sound->m_PlaySoundFlags & (PLAYSOUND_LOOP | PLAYSOUND_GETHANDLE)) != (PLAYSOUND_LOOP | PLAYSOUND_GETHANDLE)) return LT_ERROR;
    const auto result = sm_SetSoundTrackChangeFlags(this,sound,0x20);
    if (result != LT_OK) return result;
    sound->m_fTimer = 0;
    sound->m_bKill = LTTRUE;
    return LT_OK;
}

void SoundInstance::Term() {
    if (!m_Manager) return;
    if ((m_PlaySoundFlags & PLAYSOUND_ATTACHED) && m_pObject) {
        auto* object = m_pObject;
        m_pObject = nullptr;
        sm_DetachSound(m_Manager,object,this,1);
    }
    auto** changed = &m_Manager->m_pChangedSounds;
    while (*changed && *changed != this) changed = &(*changed)->m_pNextChanged;
    if (*changed) *changed = m_pNextChanged;
    if (m_pUnknown0c) {
        const auto index = m_pUnknown0c->m_nIndex;
        if (index < m_Manager->m_Unknown8fc.GetSize()) {
            auto& entry = m_Manager->m_Unknown8fc[index];
            if (entry.m_nFlags == 2 && entry.m_pRecord == this) entry = {};
        }
        sm_Free28(m_Manager,m_pUnknown0c);
    }
    m_pUnknown0c = nullptr;
    m_pNextChanged = nullptr;
    m_pObject = nullptr;
    m_Unknown2c = 0;
    m_Track.reset();
    m_Manager = nullptr;
}

LTBOOL SoundInstance::Init(CServerMgr& manager, PlaySoundInfo* info, float time, CFileRef* file,
                         std::shared_ptr<const SoundTrack> track) {
    if (!info || !file || m_Manager || !std::isfinite(time) || sound_NormalizeRequest(*info) != LT_OK) return LTFALSE;
    auto* object = info->m_hObject;
    if ((info->m_dwFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL)) &&
        (!object || !manager.m_Unknown390.m_Allocated.contains(object))) return LTFALSE;
    m_PlaySoundFlags = info->m_dwFlags;
    m_hFile = file;
    m_pObject = (m_PlaySoundFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL)) ? object : nullptr;
    m_nPriority = info->m_nPriority;
    m_fOuterRadius = info->m_fOuterRadius;
    m_fInnerRadius = info->m_fInnerRadius;
    m_nVolume = info->m_nVolume;
    m_fPitchShift = info->m_fPitchShift;
    if (m_PlaySoundFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))
        m_vPosition = (m_PlaySoundFlags & PLAYSOUND_ATTACHED) ? object->m_Pos : info->m_vPosition;
    m_UserData = info->m_UserData;
    m_Track = std::move(track);
    m_fDuration = m_Track ? m_Track->server_duration : 0;
    m_fTimer = m_fDuration + std::min(.5f,m_fDuration * .5f);
    m_fStartTime = time;
    void* id = nullptr;
    if (sm_ClaimObjectID(&manager,&id,0xffff) != LT_OK) return LTFALSE;
    m_pUnknown0c = static_cast<CObjectIDRec*>(id);
    m_Manager = &manager;
    auto& entry = manager.m_Unknown8fc[m_pUnknown0c->m_nIndex];
    entry.m_nFlags = 2; entry.m_pRecord = this;
    if ((m_PlaySoundFlags & PLAYSOUND_ATTACHED) &&
        sm_AttachSound(&manager,object,this) != LT_OK) {
        Term(); return LTFALSE;
    }
    return sm_SetSoundTrackChangeFlags(&manager,this,3) == LT_OK;
}
void SoundInstance::UpdateTimer(float seconds) {
    if (!m_Manager || !std::isfinite(seconds) || seconds < 0) return;
    m_fTimer = std::max(0.f,m_fTimer - seconds);
    if (m_bKill) return;
    if (!(m_PlaySoundFlags & (PLAYSOUND_GETHANDLE | PLAYSOUND_LOOP)) && !m_fTimer && !m_ListenerCount) {
        m_bKill = LTTRUE; return;
    }
    if (!(m_PlaySoundFlags & PLAYSOUND_ATTACHED) || !(m_PlaySoundFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D))) return;
    auto* object = GetObject();
    if (!object) {
        if (!(m_PlaySoundFlags & PLAYSOUND_GETHANDLE)) { m_fTimer = 0; m_bKill = LTTRUE; }
        return;
    }
    if (object->m_Pos.x != m_vPosition.x || object->m_Pos.y != m_vPosition.y || object->m_Pos.z != m_vPosition.z) {
        m_vPosition = object->m_Pos;
        sm_SetSoundTrackChangeFlags(m_Manager,this,2);
    }
}
