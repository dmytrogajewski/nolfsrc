#include "objtypes.h"
#include <cstdint>

LTRESULT SpriteControl::GetNumAnims(uint32& count) {
    count = 0;
    if (!m_pInstance || !m_pInstance->m_pSprite) return LT_NOTINITIALIZED;
    count = m_pInstance->m_pSprite->m_nAnims;
    return LT_OK;
}
LTRESULT SpriteControl::GetNumFrames(uint32 animation, uint32& count) {
    count = 0;
    if (!m_pInstance || !m_pInstance->m_pSprite) return LT_NOTINITIALIZED;
    const auto& sprite = *m_pInstance->m_pSprite;
    if (animation >= sprite.m_nAnims || !sprite.m_pAnims) return LT_INVALIDPARAMS;
    count = sprite.m_pAnims[animation].m_nFrames;
    return LT_OK;
}
LTRESULT SpriteControl::GetCurPos(uint32& animation, uint32& frame) {
    animation = frame = 0;
    if (!m_pInstance || !m_pInstance->m_pSprite || !m_pInstance->m_pCurAnim || !m_pInstance->m_pCurFrame) return LT_NOTINITIALIZED;
    const auto& sprite = *m_pInstance->m_pSprite;
    const auto* current = m_pInstance->m_pCurAnim;
    const auto address = reinterpret_cast<std::uintptr_t>(current), start = reinterpret_cast<std::uintptr_t>(sprite.m_pAnims);
    if (!start || address < start || (address - start) % sizeof(SpriteAnim) || (address - start) / sizeof(SpriteAnim) >= sprite.m_nAnims) return LT_INVALIDPARAMS;
    const auto pos = reinterpret_cast<std::uintptr_t>(m_pInstance->m_pCurFrame), frames = reinterpret_cast<std::uintptr_t>(current->m_pFrames);
    if (!frames || pos < frames || (pos - frames) % sizeof(uint32) || (pos - frames) / sizeof(uint32) >= current->m_nFrames) return LT_INVALIDPARAMS;
    animation = uint32((address - start) / sizeof(SpriteAnim)); frame = uint32((pos - frames) / sizeof(uint32));
    return LT_OK;
}
LTRESULT SpriteControl::SetCurPos(uint32 animation, uint32 frame) {
    if (!m_pInstance || !m_pInstance->m_pSprite) return LT_NOTINITIALIZED;
    auto& sprite = *m_pInstance->m_pSprite;
    if (animation >= sprite.m_nAnims || !sprite.m_pAnims) return LT_INVALIDPARAMS;
    auto& anim = sprite.m_pAnims[animation];
    if (frame >= anim.m_nFrames || !anim.m_pFrames || !anim.m_FrameRate || anim.m_FrameRate>1000) return LT_INVALIDPARAMS;
    const auto time = uint64(frame) * (1000 / anim.m_FrameRate);
    if (time > UINT32_MAX) return LT_INVALIDPARAMS;
    m_pInstance->m_pCurAnim = &anim; m_pInstance->m_pCurFrame = anim.m_pFrames + frame;
    m_pInstance->m_FrameTime = static_cast<uint32>(time);
    return LT_OK;
}
LTRESULT SpriteControl::GetFlags(uint32& flags) {
    flags = 0;
    if (!m_pInstance) return LT_NOTINITIALIZED;
    flags = m_pInstance->m_Unknown1b8; return LT_OK;
}
void CObjSprite::UpdateAnimation(uint32 milliseconds) {
    uint32 animation=0,frame=0;
    if (m_Control.GetCurPos(animation,frame)!=LT_OK) return;
    const auto& current=m_pSprite->m_pAnims[animation];
    if (!current.m_FrameRate || current.m_FrameRate>1000 || !current.m_nFrames || !current.m_pFrames) { m_pCurFrame=nullptr; return; }
    const auto period=1000/current.m_FrameRate;
    const auto duration=uint64(period)*current.m_nFrames;
    auto time=uint64(m_FrameTime)+((m_Unknown1b8&SC_PLAY) ? milliseconds : 0);
    if (time>=duration && !(m_Unknown1b8&SC_LOOP)) time=duration-1;
    else time%=duration;
    // Canonical sprite timing is bounded by the native field's extent.
    if (time>UINT32_MAX) return;
    m_FrameTime=static_cast<uint32>(time); m_pCurFrame=current.m_pFrames+time/period;
}
LTRESULT SpriteControl::SetFlags(uint32 flags) {
    if (!m_pInstance) return LT_NOTINITIALIZED;
    m_pInstance->m_Unknown1b8 = flags; return LT_OK;
}
