#pragma once
#include "structbank.h"
#include "ltlink.h"
#include <new>

class CObjectIDRec : public LTLink {
public:
    union { uint32 m_nIndex = 0; uint32 m_nID; };
};
using CObjIDRec = CObjectIDRec;
using CSoundIDRec = CObjectIDRec;
using CSoundClientRec = CObjectIDRec;

// Queue links and ID records share the original link pool, sized for their
// actual native types. No caller depends on a vptr or a byte offset into it.
class CLinkNodeBank {
public:
    CLinkNodeBank() { sb_Init(&m_Bank, sizeof(CObjectIDRec), 32); }
    LTLink* Allocate() {
        auto* memory = sb_Allocate(&m_Bank);
        return memory ? new (memory) LTLink : nullptr;
    }
    StructBank m_Bank;
};
extern CLinkNodeBank g_LinkNodeBank;
using CObjQueueBank = CLinkNodeBank;
inline auto& g_ObjQueueBank = g_LinkNodeBank;

extern StructBank& g_MemTrackBank;
