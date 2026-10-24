#include "engine/server/structbank.h"
#include "engine/portable/arena.h"
#include "engine/server/world_node.h"
#include "engine/server/observer.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        {
            std::array<WorldPolyRef, 3> nodes;
            require(w_GetTreeNodeQT1(nodes.data(), nodes.size(), 2) == &nodes[2], "world node lookup uses native record stride");
            require(!w_GetTreeNodeQT1(nodes.data(), nodes.size(), 3) && !w_GetTreeNodeQT1(nodes.data(), nodes.size(), 0x80000000), "invalid node indices cannot wrap through signed comparisons");
            require(w_GetTreeNodeQT1(nodes.data(), nodes.size(), 0xffffffff) == g_pWorldTreeFrontTerm &&
                    w_GetTreeNodeQT1(nodes.data(), nodes.size(), 0xfffffffe) == g_pWorldTreeBackTerm, "serialized leaf indices retain their distinct sentinels");
            nodes[0].m_Children[0] = &nodes[1];
            nodes[0].m_Children[1] = &nodes[2];
            nodes[0].m_x = 123;
            nodes[0].m_nProject = 5;
            WorldPolyRef copy(nodes[0]);
            require(copy.m_Children[0] == &nodes[1] && copy.m_Children[1] == &nodes[2] && copy.m_x == 123 && copy.m_y == 5 &&
                    copy.m_head.m_pNext == &copy.m_head && copy.m_head.m_pPrev == &copy.m_head, "node copy preserves native pointers and owns its empty list head");
        }
        {
            struct Notifications { unsigned calls = 0, destroyed = 0; } state;
            ObserverHook hook;
            ObserverNode node;
            hook.m_pFn = [](CModelSub14* owner, ObserverNode* current, void* first, void* second) {
                auto& data = *static_cast<Notifications*>(owner->m_pOwner);
                ++data.calls;
                if (!first && second && *static_cast<uint32*>(second) == 1) {
                    ++data.destroyed;
                    owner->m_pHead = current->m_pNext;
                }
            };
            node.m_pHooks = &hook;
            {
                CModelSub14 owner;
                owner.Init(&state);
                owner.m_pHead = &node;
                int update = 42;
                owner.Walk(&update, nullptr);
                require(state.calls == 1 && !state.destroyed, "shared owner dispatches through a typed callback");
            }
            require(state.calls == 2 && state.destroyed == 1, "shared owner sends one destruction notification before its storage expires");
        }
        StructBank bank;
        sb_InitAligned(&bank, 73, 7, 64);
        std::array<void*, 100> records{};
        for (auto& record : records) {
            record = sb_AllocateCleared(&bank);
            require(record && reinterpret_cast<std::uintptr_t>(record) % 64 == 0, "aligned native pool growth");
            require(sb_IsObjectAllocated(&bank, record), "allocated pool record tracked");
            auto* bytes = static_cast<unsigned char*>(record);
            for (int i = 0; i < 73; ++i) require(bytes[i] == 0, "cleared allocation spans complete record");
            bytes[72] = 255;
        }
        require(std::set<void*>(records.begin(), records.end()).size() == records.size(), "pool allocations are distinct");
        sb_Free(&bank, records[0]);
        require(!sb_IsObjectAllocated(&bank, records[0]), "released record tracked");
        require(sb_Allocate(&bank) == records[0], "pool reuses released storage");
        const auto count = bank.m_nTotalObjects;
        require(!sb_AllocateNewStructPage(&bank, std::numeric_limits<uint32>::max()) && bank.m_nTotalObjects == count,
                "oversized pool growth preserves existing records");
        sb_FreeAll(&bank);
        for (auto record : records) require(!sb_IsObjectAllocated(&bank, record), "pool reset releases all records");
        std::set<void*> reused;
        for (uint32 i = 0; i < count; ++i) reused.insert(sb_Allocate(&bank));
        require(reused.size() == count && !reused.contains(nullptr), "pool reset retains usable pages");
        sb_Term(&bank); sb_Term(&bank);
        require(!bank.m_pFreeList && bank.m_nPages == 0, "pool teardown is repeatable");
        sb_Init(&bank, 1, 3);
        auto* tiny = sb_AllocateCleared(&bank);
        require(tiny && bank.m_AlignedStructSize >= sizeof(void*) && reinterpret_cast<std::uintptr_t>(tiny) % alignof(std::max_align_t) == 0,
                "small records reserve an aligned native free-list pointer");
        sb_Free(&bank, tiny);
        int foreign = 0;
        require(!sb_IsObjectAllocated(&bank, &foreign), "foreign pointer is outside pool");
        {
            struct Delegate final : LAlloc {
                int blocks = 0;
                bool refuse = false;
                void* Alloc(uint32 size) override {
                    if (refuse) return nullptr;
                    auto* memory = LAlloc::Alloc(size);
                    if (memory) ++blocks;
                    return memory;
                }
                void Free(void* memory) override { if (memory) --blocks; LAlloc::Free(memory); }
            } delegate;
            {
                CMoMemBlock arena;
                require(arena.Alloc(&delegate, 256) && delegate.blocks == 1, "model arena owns one backing allocation");
                require(arena.Alloc(1) && arena.Alloc(3), "model arena accepts odd record sizes");
                auto* aligned = arena.Alloc(32);
                require(aligned && reinterpret_cast<std::uintptr_t>(aligned) % alignof(std::max_align_t) == 0, "model arena preserves native alignment");
                auto used = arena.m_Unknown_0c;
                require(!arena.Alloc(std::numeric_limits<uint32>::max()) && arena.m_Unknown_0c == used, "arena overflow preserves cursor");
                arena.Free(aligned);
                require(delegate.blocks == 1, "individual arena frees retain backing allocation");
                delegate.refuse = true;
                require(!arena.Alloc(&delegate, 512) && arena.m_Unknown_0c == used && delegate.blocks == 1, "failed arena replacement preserves storage");
            }
            require(delegate.blocks == 0, "model arena teardown releases delegate allocation");
        }
        std::cout << "native pool alignment, growth, reuse, bounds, and lifetime passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
