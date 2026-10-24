#pragma once
#include "collection.h"
#include "ltcodes.h"
#include <limits>

class ILTStream;
struct CLALight {
    uint16 m_n00 = 0, m_n02 = 0;
};
static_assert(sizeof(CLALight) == 4);
struct CLAFrameEntry {
    void* m_pData = nullptr;
    uint32 m_nSize = 0;
};
struct LightAnim {
    char m_Name[32]{};
    LTBOOL m_bShadowMap = LTFALSE;
    CLAFrameEntry** m_ppFrames = nullptr;
    uint32 m_nFrames = 0;
    CLALight* m_pLights = nullptr;
    uint32 m_nLights = 0;
    uint32 m_iFrames[2]{std::numeric_limits<uint32>::max(), std::numeric_limits<uint32>::max()};
    float m_fPercentBetween = 0, m_fBlendPercent = 1;
    LTVector m_vLightPos{0, 0, 0}, m_vLightColor{0, 0, 0};
    float m_fLightRadius = 0;
};
using CLARecord = LightAnim;
inline constexpr uint32 WD_HAS_BASEANIM = 0x04;

// The five pools own the records and their pointed-to spans together. File
// counts describe serialized records, never sizeof(native pointer records).
class CWorldData {
public:
    CWorldData() = default;
    CWorldData(const CWorldData&) = delete;
    CWorldData& operator=(const CWorldData&) = delete;
    virtual ~CWorldData() = default;
    virtual LightAnim* FindLightAnim(const char* name, uint32* index = nullptr);
    void TermArrays();
    LTRESULT LoadObjects(ILTStream* stream);
    uint64 lighting_generation() const { return generation_; }

    CMoArray<LightAnim> m_Anims;
    CMoArray<CLALight> m_Lights;
    CMoArray<CLAFrameEntry*> m_Frames;
    CMoArray<uint8> m_Data;
    CMoArray<CLAFrameEntry> m_FrameEntries;
    uint32 m_nObjectsOffset = 0;
    uint32 m_Flags180 = 0;
private:
    uint64 generation_=1;
    void SwapStorage(CWorldData& other) noexcept;
};
