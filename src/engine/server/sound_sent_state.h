#pragma once
#include "ltbasetypes.h"

constexpr uint8 SNDSENT_FLAGS = 1, SNDSENT_BYTE04 = 2, SNDSENT_RANGE = 4;
struct CSoundSentState {
    uint8 m_ChangeFlags = 7;
    uint16 m_Flags = 0;
    uint8 m_Unknown04 = 0;
    uint16 m_nRange = 0;
    uint8 m_nInnerRatio = 0;
};
inline void snd_MergeSentState(CSoundSentState* stored, const CSoundSentState* next) {
    if ((stored->m_ChangeFlags & SNDSENT_FLAGS) || stored->m_Flags != next->m_Flags) {
        stored->m_ChangeFlags |= SNDSENT_FLAGS; stored->m_Flags = next->m_Flags;
    }
    if ((stored->m_ChangeFlags & SNDSENT_BYTE04) || stored->m_Unknown04 != next->m_Unknown04) {
        stored->m_ChangeFlags |= SNDSENT_BYTE04; stored->m_Unknown04 = next->m_Unknown04;
    }
    if ((stored->m_ChangeFlags & SNDSENT_RANGE) || stored->m_nRange != next->m_nRange || stored->m_nInnerRatio != next->m_nInnerRatio) {
        stored->m_ChangeFlags |= SNDSENT_RANGE; stored->m_nRange = next->m_nRange; stored->m_nInnerRatio = next->m_nInnerRatio;
    }
}
CSoundSentState* snd_FindSentState(class Client*, uint16 fileID);
