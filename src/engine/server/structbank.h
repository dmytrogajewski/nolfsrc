#pragma once
#include "ltbasetypes.h"
#include <cstddef>

// Raw, fixed-size storage. Callers construct and destroy their own records.
// Every slot satisfies the bank's alignment, including the intrusive link.
class StructBank {
public:
    StructBank() = default;
    ~StructBank();
    StructBank(const StructBank&) = delete;
    StructBank& operator=(const StructBank&) = delete;
    uint32 m_StructSize = 0;
    union { uint32 m_AlignedStructSize = 0; uint32 m_Unknown04; };
    union { uint32 m_CacheSize = 0; uint32 m_Unknown08; };
    uint32 m_nPages = 0, m_nTotalObjects = 0;
    struct Page;
    Page* m_PageHead = nullptr;
    union { void* m_pFreeList = nullptr; void* m_FreeListHead; };
    std::size_t m_Alignment = alignof(std::max_align_t);
};
void sb_Init(StructBank*, uint32 size, uint32 cache);
void sb_InitAligned(StructBank*, uint32 size, uint32 cache, std::size_t alignment);
int sb_Init2(StructBank*, uint32 size, uint32 cache, uint32 preallocate);
int sb_AllocateNewStructPage(StructBank*, uint32 count);
void sb_Grow(StructBank*, uint32 count);
void sb_Term(StructBank*);
void sb_Term2(StructBank*, int check);
void sb_FreeChunks(StructBank*, int check);
void sb_FreeAll(StructBank*);
void* sb_AllocateCleared(StructBank*);
LTBOOL sb_IsObjectAllocated(StructBank*, void*);
extern int g_bDebugStructBanks;

inline void* sb_Allocate(StructBank* bank) {
    if (!bank->m_pFreeList) sb_Grow(bank, bank->m_CacheSize);
    auto* pointer = bank->m_pFreeList;
    if (pointer) bank->m_pFreeList = *static_cast<void**>(pointer);
    return pointer;
}
inline void sb_Free(StructBank* bank, void* pointer) {
    if (!pointer) return;
    *static_cast<void**>(pointer) = bank->m_pFreeList;
    bank->m_pFreeList = pointer;
}
inline void* sb_Allocate_z(StructBank* bank) { return sb_AllocateCleared(bank); }
#define DECLARE_STRUCTBANK(name) StructBank name
