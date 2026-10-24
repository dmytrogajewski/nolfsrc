#include "structbank.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

int g_bDebugStructBanks = 0;
struct StructBank::Page {
    Page* next = nullptr;
    void* data = nullptr;
    uint32 count = 0;
};
StructBank::~StructBank() { sb_Term(this); }
void sb_Term(StructBank* bank) {
    while (auto* page = bank->m_PageHead) {
        bank->m_PageHead = page->next;
        ::operator delete(page->data, std::align_val_t(bank->m_Alignment));
        delete page;
    }
    bank->m_pFreeList = nullptr;
    bank->m_nPages = bank->m_nTotalObjects = 0;
}
void sb_Term2(StructBank* bank, int) { sb_Term(bank); }
void sb_FreeChunks(StructBank* bank, int) { sb_Term(bank); }
void sb_InitAligned(StructBank* bank, uint32 size, uint32 cache, std::size_t alignment) {
    alignment = std::max(alignment, alignof(void*));
    if (!size || !cache || (alignment & (alignment - 1)) || alignment > 4096)
        throw std::invalid_argument("invalid struct bank size, cache, or alignment");
    const auto minimum = std::max<std::size_t>(size, sizeof(void*));
    const auto stride = (minimum + alignment - 1) & ~(alignment - 1);
    if (stride > std::numeric_limits<uint32>::max()) throw std::length_error("struct bank stride overflow");
    sb_Term(bank);
    bank->m_StructSize = size; bank->m_AlignedStructSize = static_cast<uint32>(stride);
    bank->m_CacheSize = cache; bank->m_Alignment = alignment;
}
void sb_Init(StructBank* bank, uint32 size, uint32 cache) {
    sb_InitAligned(bank, size, cache, alignof(std::max_align_t));
}
int sb_AllocateNewStructPage(StructBank* bank, uint32 count) {
    if (!count) return 1;
    constexpr std::size_t max_page = 64 * 1024 * 1024;
    if (!bank->m_AlignedStructSize || count > max_page / bank->m_AlignedStructSize ||
        count > std::numeric_limits<uint32>::max() - bank->m_nTotalObjects) return 0;
    auto* page = new (std::nothrow) StructBank::Page;
    if (!page) return 0;
    page->data = ::operator new(std::size_t(count) * bank->m_AlignedStructSize,
                               std::align_val_t(bank->m_Alignment), std::nothrow);
    if (!page->data) { delete page; return 0; }
    page->count = count; page->next = bank->m_PageHead;
    bank->m_PageHead = page;
    ++bank->m_nPages; bank->m_nTotalObjects += count;
    auto* data = static_cast<std::byte*>(page->data);
    for (uint32 i = 0; i < count; ++i) sb_Free(bank, data + std::size_t(i) * bank->m_AlignedStructSize);
    return 1;
}
void sb_Grow(StructBank* bank, uint32 count) { sb_AllocateNewStructPage(bank, count); }
int sb_Init2(StructBank* bank, uint32 size, uint32 cache, uint32 preallocate) {
    sb_Init(bank, size, cache);
    return sb_AllocateNewStructPage(bank, preallocate);
}
void sb_FreeAll(StructBank* bank) {
    bank->m_pFreeList = nullptr;
    for (auto* page = bank->m_PageHead; page; page = page->next) {
        auto* data = static_cast<std::byte*>(page->data);
        for (uint32 i = 0; i < page->count; ++i) sb_Free(bank, data + std::size_t(i) * bank->m_AlignedStructSize);
    }
}
void* sb_AllocateCleared(StructBank* bank) {
    auto* data = sb_Allocate(bank);
    if (data) std::memset(data, 0, bank->m_StructSize);
    return data;
}
LTBOOL sb_IsObjectAllocated(StructBank* bank, void* object) {
    // Diagnostic only: never infer state from the caller's payload bytes.
    bool member = false;
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    for (auto* page = bank->m_PageHead; page; page = page->next) {
        const auto begin = reinterpret_cast<std::uintptr_t>(page->data);
        const auto length = std::size_t(page->count) * bank->m_AlignedStructSize;
        if (address >= begin && address - begin < length && (address - begin) % bank->m_AlignedStructSize == 0) { member = true; break; }
    }
    if (!member) return LTFALSE;
    for (auto* free = bank->m_pFreeList; free; free = *static_cast<void**>(free)) if (free == object) return LTFALSE;
    return LTTRUE;
}
