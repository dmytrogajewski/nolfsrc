#include "arena.h"
#include <cstddef>
#include <limits>

CMoMemBlock::~CMoMemBlock() { Term(); }
void CMoMemBlock::Term() {
    if (m_pData) m_pAlloc->Free(m_pData);
    m_pData = nullptr; m_pAlloc = nullptr; m_Unknown_0c = m_nSize = 0;
}
LTBOOL CMoMemBlock::Alloc(LAlloc* delegate, uint32 size) {
    if (!delegate || delegate == this) return LTFALSE;
    void* data = size ? delegate->Alloc(size) : nullptr;
    if (size && !data) return LTFALSE;
    Term();
    m_pAlloc = delegate; m_pData = data; m_nSize = size;
    return LTTRUE;
}
void* CMoMemBlock::Alloc(uint32 size) {
    if (!size) return nullptr;
    constexpr std::size_t alignment = alignof(std::max_align_t);
    const auto offset = (std::size_t(m_Unknown_0c) + alignment - 1) & ~(alignment - 1);
    if (offset > m_nSize || size > m_nSize - offset) return nullptr;
    m_Unknown_0c = static_cast<uint32>(offset + size);
    return static_cast<std::byte*>(m_pData) + offset;
}
