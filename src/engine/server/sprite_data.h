#pragma once
#include "ltbasetypes.h"

struct SpriteAnim {
    uint32* m_pFrames = nullptr;
    uint32 m_nFrames = 0;
    uint32 m_FrameRate = 0;
};
struct Sprite {
    SpriteAnim* m_pAnims = nullptr;
    uint32 m_nAnims = 0;
};
