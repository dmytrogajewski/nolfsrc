#include "sound_request.h"
#include <cstring>

LTRESULT sound_NormalizeFilename(char* name,std::size_t capacity) {
    if (!name) return LT_INVALIDPARAMS;
    const auto* end=static_cast<const char*>(std::memchr(name,0,capacity));
    if (!end) return LT_INVALIDPARAMS;
    // Old savegames retain authored trailing blanks even after the level's
    // sound property is corrected. They are not part of the asset filename.
    auto length=std::size_t(end-name);
    while (length && (name[length-1]==' ' || name[length-1]=='\t')) --length;
    name[length]=0;
    if (!length) return LT_INVALIDPARAMS;
    // The freighter fans use '|' where the original Windows asset path
    // requires a separator. Normalize restored copies as well as fresh data.
    std::replace(name,name+length,'|','\\');
    return LT_OK;
}
LTRESULT sound_NormalizeRequest(PlaySoundInfo& info) {
    if (sound_NormalizeFilename(info.m_szSoundName,sizeof(info.m_szSoundName))!=LT_OK) return LT_INVALIDPARAMS;
    const bool spatial = (info.m_dwFlags & (PLAYSOUND_AMBIENT | PLAYSOUND_3D)) != 0;
    if (spatial && (!std::isfinite(info.m_fOuterRadius) || !std::isfinite(info.m_fInnerRadius))) return LT_INVALIDPARAMS;
    if (spatial && !(info.m_dwFlags & PLAYSOUND_ATTACHED) &&
        (!std::isfinite(info.m_vPosition.x) || !std::isfinite(info.m_vPosition.y) || !std::isfinite(info.m_vPosition.z))) return LT_INVALIDPARAMS;
    if ((info.m_dwFlags & PLAYSOUND_CTRL_PITCH) && (!std::isfinite(info.m_fPitchShift) || info.m_fPitchShift < 0)) return LT_INVALIDPARAMS;
    if (spatial) {
        info.m_fOuterRadius = std::clamp(info.m_fOuterRadius,1.f,65000.f);
        info.m_fInnerRadius = std::clamp(info.m_fInnerRadius,1.f,65000.f);
    } else info.m_fOuterRadius = info.m_fInnerRadius = 0;
    if (!spatial || (info.m_dwFlags & PLAYSOUND_ATTACHED)) info.m_vPosition.Init(0,0,0);
    if (!(info.m_dwFlags & PLAYSOUND_CTRL_PITCH)) info.m_fPitchShift = 1;
    if (info.m_fPitchShift == 1) info.m_dwFlags &= ~PLAYSOUND_CTRL_PITCH;
    if (!(info.m_dwFlags & PLAYSOUND_CTRL_VOL)) info.m_nVolume = 100;
    info.m_nVolume = std::min<uint8>(info.m_nVolume,100);
    if (info.m_nVolume == 100) info.m_dwFlags &= ~PLAYSOUND_CTRL_VOL;
    if (info.m_dwFlags & PLAYSOUND_ATTACHED) info.m_dwFlags &= ~PLAYSOUND_CLIENTLOCAL;
    return LT_OK;
}
