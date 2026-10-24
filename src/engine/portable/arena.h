#pragma once
#include "engine/server/collection.h"

// Model records share one allocation. Individual frees leave storage in place
// until the model has destroyed its records and releases the complete arena.
class CMoMemBlock : public LAlloc {
public:
    CMoMemBlock() = default;
    ~CMoMemBlock() override;
    CMoMemBlock(const CMoMemBlock&) = delete;
    CMoMemBlock& operator=(const CMoMemBlock&) = delete;
    LTBOOL Alloc(LAlloc* delegate, uint32 size);
    void* Alloc(uint32 size) override;
    void Free(void*) override {}
    void Term();
    LAlloc* m_pAlloc = nullptr;
    void* m_pData = nullptr;
    uint32 m_Unknown_0c = 0, m_nSize = 0;
};
