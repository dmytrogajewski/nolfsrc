#pragma once
#include "ltbasedefs.h"
class CWorldBsp;
class CWorldSurface;
class CServerWorld;
struct WorldObjectInfoQT1 {
    LTVector m_vO, m_vP, m_vQ;
    CServerWorld* m_pServerWorld;
    CWorldBsp* m_pBsp;
    CWorldSurface* m_pSurface;
};
class WorldClassQT1 {
public:
    void* (*m_pMake)(WorldObjectInfoQT1*, int, char**) = nullptr;
    void* m_Unknown04 = nullptr;
    void (*m_pFree)(void*) = nullptr;
    WorldClassQT1* m_pNext = nullptr;
    const char* m_Name = "";
};
struct WorldObjectRecQT1 {
    ~WorldObjectRecQT1() { if (m_pObject && m_pClass && m_pClass->m_pFree) m_pClass->m_pFree(m_pObject); }
    CWorldBsp* m_pBsp = nullptr;
    CWorldSurface* m_pSurface = nullptr;
    void* m_pObject = nullptr;
    WorldClassQT1* m_pClass = nullptr;
    WorldObjectRecQT1* m_pNext = nullptr;
};
void w_AddWorldObjectQT1(CServerWorld*, WorldObjectRecQT1*);
