#pragma once
#include "worldbsp.h"
#include <span>

class CServerWorldRec {
public:
    CServerWorldRec();
    ~CServerWorldRec();
    CServerWorldRec(const CServerWorldRec&) = delete;
    CServerWorldRec& operator=(const CServerWorldRec&) = delete;
    void Clear();
    void Term();
    uint32 m_Flags = 0;
    CWorldBsp* m_pWorldBsp = nullptr;
    CWorldBsp* m_pOriginal = nullptr;
    CWorldBsp* m_pCurrent = nullptr;
};

bool w_ResolvePolyKeys(CWorldBsp& world, std::span<CServerWorldRec* const> models);
