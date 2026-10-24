#include "dhash.h"
#include "ltlink.h"
#include "engine/portable/record_pool.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_set>
#include <vector>

class CHashElement;
class CHashSlot {
public:
    CHashSlot() { m_Head.Init2(nullptr); }
    CHashTable* m_pTable = nullptr;
    uint32 m_nIndex = 0, m_nElements = 0;
    LTLink m_Head;
};
class CHashElement {
public:
    CHashElement() { m_Link.Init2(this); }
    LTLink m_Link;
    CHashSlot* m_pSlot = nullptr;
    void* m_pData = nullptr;
    uint32 m_nKeyLen = 0;
    std::array<char,2> m_Word{};
    std::vector<char> m_Key;
    char* Key() { return m_Key.empty() ? m_Word.data() : m_Key.data(); }
};
namespace { std::shared_ptr<nolf::RecordPool<CHashElement>> word_pool(); }
class CHashTable {
public:
    CHashTable(uint32 slots, uint32 kind) : m_nKeyType(kind), m_nSlots(slots), m_Slots(new CHashSlot[slots]),
        m_WordPool(kind == DHASHKEY_WORD ? word_pool() : nullptr) {
        for (uint32 i = 0; i < slots; ++i) { m_Slots[i].m_pTable = this; m_Slots[i].m_nIndex = i; }
    }
    ~CHashTable();
    uint32 m_nKeyType, m_nSlots;
    std::unique_ptr<CHashSlot[]> m_Slots;
    std::unordered_set<CHashElement*> m_Elements;
    // Tables keep their pool alive independently of global shutdown order.
    std::shared_ptr<nolf::RecordPool<CHashElement>> m_WordPool;
};

namespace {
std::shared_ptr<nolf::RecordPool<CHashElement>> word_pool() {
    static auto pool = std::make_shared<nolf::RecordPool<CHashElement>>();
    return pool;
}
bool valid(const CHashTable* table, const void* key, uint32 length) {
    return table && length <= UINT16_MAX && (!length || key) && (table->m_nKeyType != DHASHKEY_WORD || length == 2);
}
uint8 fold(uint8 character, uint32 kind) {
    if ((kind == DHASHKEY_STRING || kind == DHASHKEY_FILENAME) && character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (kind == DHASHKEY_FILENAME && character == '/') character = '\\';
    return character;
}
uint32 hash(const void* key, uint32 length, uint32 kind) {
    const auto* bytes = static_cast<const uint8*>(key);
    if (kind == DHASHKEY_WORD) return uint32(bytes[0]) | (uint32(bytes[1]) << 8);
    uint32 result = 0;
    // Retail hashes sign-extend bytes above ASCII. Spell that conversion
    // explicitly so char's implementation-defined signedness cannot alter it.
    for (uint32 i = 0; i < length; ++i) {
        const auto byte = fold(bytes[i],kind);
        const auto signed_byte = byte < 128 ? int32(byte) : int32(byte) - 256;
        result += static_cast<uint32>(signed_byte) * i;
    }
    return result;
}
bool equal(CHashElement* element, const void* key, uint32 length, uint32 kind) {
    if (element->m_nKeyLen != length) return false;
    const auto* a = reinterpret_cast<const uint8*>(element->Key());
    const auto* b = static_cast<const uint8*>(key);
    for (uint32 i = 0; i < length; ++i) if (fold(a[i],kind) != fold(b[i],kind)) return false;
    return true;
}
CHashElement* find(CHashTable* table, CHashSlot& slot, LTLink* link, const void* key, uint32 length) {
    for (; link != &slot.m_Head; link = link->m_pNext) {
        auto* element = static_cast<CHashElement*>(link->m_pData);
        if (equal(element,key,length,table->m_nKeyType)) return element;
    }
    return nullptr;
}
CHashElement* next(CHashSlot& slot, LTLink* link) {
    auto* current = &slot;
    for (link = link->m_pNext; link == &current->m_Head; link = current->m_Head.m_pNext) {
        if (current->m_nIndex + 1 == current->m_pTable->m_nSlots) return nullptr;
        ++current;
    }
    return static_cast<CHashElement*>(link->m_pData);
}
void destroy(CHashTable* table, CHashElement* element) {
    element->m_Link.Remove(); --element->m_pSlot->m_nElements;
    if (table->m_nKeyType == DHASHKEY_WORD) table->m_WordPool->Free(element);
    else delete element;
}
}
CHashTable::~CHashTable() { for (auto* element : m_Elements) destroy(this,element); }
void* dhash_Create(int slots, int kind) {
    // Bound allocation before conversion or multiplication. One bucket is a
    // valid table; larger tables keep the original modulo(slots - 1) order.
    constexpr uint32 maximum_slots = 64 * 1024 * 1024 / sizeof(CHashSlot);
    if (slots < 1 || uint32(slots) > maximum_slots || kind < 0 || kind >= DHASHKEY_COUNT) return nullptr;
    try { return new CHashTable(static_cast<uint32>(slots),static_cast<uint32>(kind)); }
    catch (const std::bad_alloc&) { return nullptr; }
}
void dhash_Free(void* handle) { delete static_cast<CHashTable*>(handle); }
uint32 dhash_GetSlot(CHashTable* table, const void* key, uint32 length) {
    if (!valid(table,key,length)) return UINT32_MAX;
    if (table->m_nSlots == 1) return 0;
    return hash(key,length,table->m_nKeyType) % (table->m_nSlots - 1);
}
void* dhash_AddElement(void* handle, const void* key, uint32 length) {
    auto* table = static_cast<CHashTable*>(handle);
    if (!valid(table,key,length)) return nullptr;
    CHashElement* element = nullptr;
    try {
        element = table->m_nKeyType == DHASHKEY_WORD ? table->m_WordPool->Allocate() : new CHashElement;
        if (!element) return nullptr;
        if (length > element->m_Word.size()) element->m_Key.resize(length);
        if (length) std::memcpy(element->Key(),key,length);
        element->m_nKeyLen = length;
        element->m_pSlot = &table->m_Slots[dhash_GetSlot(table,key,length)];
        table->m_Elements.insert(element);
    } catch (const std::bad_alloc&) {
        if (element) { if (table->m_nKeyType == DHASHKEY_WORD) table->m_WordPool->Free(element); else delete element; }
        return nullptr;
    }
    element->m_pSlot->m_Head.AddAfter(&element->m_Link);
    ++element->m_pSlot->m_nElements;
    return element;
}
void* dhash_FindElement(void* handle, const void* key, uint32 length) {
    auto* table = static_cast<CHashTable*>(handle);
    if (!valid(table,key,length)) return nullptr;
    auto& slot = table->m_Slots[dhash_GetSlot(table,key,length)];
    return find(table,slot,slot.m_Head.m_pNext,key,length);
}
void* dhash_FindNextElement(void* handle, void* previous, const void* key, uint32 length) {
    auto* table = static_cast<CHashTable*>(handle);
    auto* element = static_cast<CHashElement*>(previous);
    if (!valid(table,key,length) || !table->m_Elements.contains(element)) return nullptr;
    return find(table,*element->m_pSlot,element->m_Link.m_pNext,key,length);
}
void dhash_Remove(void* handle, void* item) {
    auto* table = static_cast<CHashTable*>(handle);
    auto* element = static_cast<CHashElement*>(item);
    if (!table || !table->m_Elements.erase(element)) return;
    destroy(table,element);
}
void* dhash_GetFirst(void* handle) {
    auto* table = static_cast<CHashTable*>(handle);
    return table ? next(table->m_Slots[0],&table->m_Slots[0].m_Head) : nullptr;
}
void* dhash_GetNext(void** iterator) {
    if (!iterator || !*iterator) return nullptr;
    auto* current = static_cast<CHashElement*>(*iterator);
    *iterator = next(*current->m_pSlot,&current->m_Link);
    return current;
}
char* dhash_GetKey(void* handle, uint32* length) {
    auto* element = static_cast<CHashElement*>(handle);
    if (length) *length = element ? element->m_nKeyLen : 0;
    return element ? element->Key() : nullptr;
}
void* dhash_GetData(void* handle) { return handle ? static_cast<CHashElement*>(handle)->m_pData : nullptr; }
void dhash_SetData(void* handle, void* data) { if (handle) static_cast<CHashElement*>(handle)->m_pData = data; }
