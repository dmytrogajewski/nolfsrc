#include "engine/server/dhash.h"
#include "engine/server/object_data_pool.h"
#include "engine/portable/assets.h"
#include <array>
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
using Table = std::unique_ptr<void,decltype(&dhash_Free)>;
Table table(int count, int kind) { return Table(dhash_Create(count,kind),dhash_Free); }
struct alignas(128) OwnedRecord {
    static inline unsigned created = 0, destroyed = 0;
    static inline std::function<void()> construct, destruct;
    std::unique_ptr<uint8[]> bytes{new uint8[37]{}};
    OwnedRecord() { if (construct) construct(); ++created; }
    ~OwnedRecord() { ++destroyed; if (destruct) destruct(); }
};
void records() {
    nolf::RecordPool<OwnedRecord> records;
    auto* a = records.Allocate(); auto* b = records.Allocate();
    require(a && b && records.Size() == 2 && reinterpret_cast<std::uintptr_t>(a) % alignof(OwnedRecord) == 0,
            "typed records own aligned native storage");
    require(records.Free(a) && !records.Free(a), "pool rejects duplicate frees without reading dead records");
    OwnedRecord::destruct = [&] { records.Term(); require(!records.Allocate(), "pending termination rejects reentrant allocation"); };
    records.Free(b);
    OwnedRecord::destruct = {};
    require(!records.Size() && !records.m_Bank.m_nPages && OwnedRecord::created == OwnedRecord::destroyed,
            "termination requested by a destructor waits until active storage is no longer in use");
    require(records.Allocate(), "terminated typed pool can allocate again");
    OwnedRecord::construct = [&] { records.Term(); };
    require(!records.Allocate() && !records.Size() && !records.m_Bank.m_nPages,
            "termination during construction cleans unpublished and existing records");
    OwnedRecord::construct = [] { throw std::runtime_error("constructor failure"); };
    require(!records.Allocate() && !records.Size(), "throwing record construction returns its raw slot");
    OwnedRecord::construct = {};
    auto* fresh = records.Allocate();
    require(fresh && records.Free(fresh), "construction failure leaves the pool reusable");
    records.Term(); records.Term();
    require(OwnedRecord::created == OwnedRecord::destroyed, "every constructed record is destroyed exactly once");

    CAbstractIO* packet = nullptr; msg_AllocPacket(&packet,32,32);
    require(packet, "object-state reference fixture allocates a real message");
    CMessageBank state;
    auto* data = state.Allocate();
    require(data && !data->m_SFXMessage.m_pMsg && !data->m_pClass && state.Size() == 1,
            "object-state bank constructs native records");
    data->m_SFXMessage.Assign(static_cast<ServerMessage*>(packet));
    require(packet->m_nRefCount == 2, "object state owns its late-join message reference");
    state.Term(); state.Term();
    require(packet->m_nRefCount == 1 && state.Size() == 0, "bank termination releases retained messages before freeing pages");
    data = state.Allocate();
    require(data && state.Free(data) && !state.Free(data), "object-state bank restarts and rejects duplicate release");
    packet->Release();
}
void keys() {
    for (int kind = 0; kind < DHASHKEY_COUNT; ++kind) {
        require(!dhash_Create(0,kind) && !dhash_Create(-1,kind) && !dhash_Create(INT_MAX,kind), "hash allocation validates slot bounds before multiplication");
        auto one = table(1,kind); require(bool(one), "single-bucket tables are supported");
        uint8 unaligned[]{0,0x34,0x12}; const char text[] = "Case/Key";
        const void* key = kind == DHASHKEY_WORD ? static_cast<const void*>(unaligned + 1) : text;
        const uint32 length = kind == DHASHKEY_WORD ? 2 : sizeof(text);
        auto* element = dhash_AddElement(one.get(),key,length);
        require(element && dhash_FindElement(one.get(),key,length) == element, "single-bucket lookup avoids division by zero");
        require(dhash_GetSlot(static_cast<CHashTable*>(one.get()),key,length) == 0, "single-bucket hash index is zero");
        if (kind == DHASHKEY_WORD) {
            require(!dhash_AddElement(one.get(),key,1) && !dhash_AddElement(one.get(),key,3), "word keys retain their exact two-byte contract");
        }
    }
    require(!dhash_Create(3,-1) && !dhash_Create(3,DHASHKEY_COUNT), "invalid hash kind is rejected");
    auto folded = table(17,DHASHKEY_STRING), literal = table(17,DHASHKEY_NOCASE), paths = table(17,DHASHKEY_FILENAME);
    const char upper[] = "Archer", lower[] = "archer";
    auto* folded_key = dhash_AddElement(folded.get(),upper,sizeof(upper));
    auto* literal_key = dhash_AddElement(literal.get(),upper,sizeof(upper));
    require(folded_key && literal_key && dhash_FindElement(folded.get(),lower,sizeof(lower)) == folded_key &&
            !dhash_FindElement(literal.get(),lower,sizeof(lower)), "numeric string modes preserve original case-folding behavior");
    const char path[] = "Worlds/SinglePlayer\\M1.DAT", equivalent[] = "worlds\\singleplayer/m1.dat";
    auto* path_key = dhash_AddElement(paths.get(),path,sizeof(path));
    require(path_key && dhash_FindElement(paths.get(),equivalent,sizeof(equivalent)) == path_key, "filename hashing folds ASCII case and both separators");
    const uint8 extended[]{0xE1,0xFF,0x80,0};
    auto* extended_key = dhash_AddElement(literal.get(),extended,sizeof(extended));
    require(extended_key && dhash_FindElement(literal.get(),extended,sizeof(extended)) == extended_key &&
            dhash_GetSlot(static_cast<CHashTable*>(literal.get()),extended,sizeof(extended)) == 15,
            "extended-byte hash arithmetic retains original signed-byte semantics");
    const char binary[]{'a',0,'b'}, different[]{'a',0,'c'};
    auto* embedded = dhash_AddElement(literal.get(),binary,sizeof(binary));
    require(embedded && !dhash_FindElement(literal.get(),different,sizeof(different)), "binary keys compare their full explicit length");
    require(dhash_AddElement(literal.get(),nullptr,0) && dhash_FindElement(literal.get(),nullptr,0), "empty byte keys do not dereference null");
    require(!dhash_AddElement(literal.get(),nullptr,1) && !dhash_FindElement(nullptr,binary,3), "invalid key pointers and null tables are rejected");
    std::vector<char> largest(UINT16_MAX,'q');
    auto* long_key = dhash_AddElement(literal.get(),largest.data(),static_cast<uint32>(largest.size()));
    uint32 length = 0;
    require(long_key && std::memcmp(dhash_GetKey(long_key,&length),largest.data(),largest.size()) == 0 && length == largest.size(),
            "largest supported key is owned without header overlap or length truncation");
    require(!dhash_AddElement(literal.get(),largest.data(),UINT16_MAX + 1u), "oversized key is rejected before reading its bytes");
    const auto* stable = dhash_GetKey(folded_key,nullptr);
    for (uint32 i = 0; i < 1000; ++i) { const auto name = std::to_string(i); require(dhash_AddElement(folded.get(),name.c_str(),uint32(name.size()+1)), "hash storage grows"); }
    require(stable == dhash_GetKey(folded_key,nullptr) && std::strcmp(stable,upper) == 0, "interned key pointers survive table growth");
}
void iteration() {
    auto words = table(17,DHASHKEY_WORD), other = table(7,DHASHKEY_WORD);
    uint16 a = 1, b = 17, c = 2;
    auto* first = dhash_AddElement(words.get(),&a,2);
    auto* second = dhash_AddElement(words.get(),&b,2);
    auto* third = dhash_AddElement(words.get(),&c,2);
    int payload = 42; dhash_SetData(first,&payload);
    require(dhash_GetData(first) == &payload && dhash_GetData(nullptr) == nullptr, "payload pointers retain their native width");
    void* iterator = dhash_GetFirst(words.get());
    require(dhash_GetNext(&iterator) == second && dhash_GetNext(&iterator) == first && dhash_GetNext(&iterator) == third &&
            !iterator && !dhash_GetNext(&iterator), "bucket iteration preserves original modulus and newest-first collision order");
    auto* duplicate = dhash_AddElement(words.get(),&a,2);
    require(dhash_FindElement(words.get(),&a,2) == duplicate && dhash_FindNextElement(words.get(),duplicate,&a,2) == first &&
            !dhash_FindNextElement(words.get(),first,&a,2), "duplicate keys retain their original lookup chain");
    dhash_Remove(other.get(),first);
    require(dhash_FindElement(words.get(),&a,2) == duplicate && !dhash_FindNextElement(other.get(),first,&a,2),
            "another table cannot remove or continue an element");
    iterator = dhash_GetFirst(words.get()); unsigned removed = 0;
    while (auto* element = dhash_GetNext(&iterator)) { dhash_Remove(words.get(),element); dhash_Remove(words.get(),element); ++removed; }
    require(removed == 4 && !dhash_GetFirst(words.get()), "iteration can remove current elements and rejects duplicate removal");
    for (uint16 key = 0; key < 1000; ++key) require(dhash_AddElement(words.get(),&key,2), "pooled word elements recycle across pages");
    for (uint16 key = 0; key < 1000; ++key) require(dhash_FindElement(words.get(),&key,2), "all recycled word records remain intact");
}
void parallel_tables() {
    std::array<std::thread,8> threads;
    std::array<bool,8> good{};
    for (unsigned i = 0; i < threads.size(); ++i) threads[i] = std::thread([&,i] {
        auto words = table(127,DHASHKEY_WORD);
        if (!words) return;
        for (uint16 key = 0; key < 1000; ++key) if (!dhash_AddElement(words.get(),&key,2)) return;
        for (uint16 key = 0; key < 1000; ++key) if (!dhash_FindElement(words.get(),&key,2)) return;
        good[i] = true;
    });
    for (auto& thread : threads) thread.join();
    require(std::all_of(good.begin(),good.end(),[](bool value) { return value; }), "independent tables safely share the native word pool across threads");
}
void assets(const std::filesystem::path& path) {
    nolf::Assets resources; resources.mount_game(path);
    auto files = table(500,DHASHKEY_FILENAME);
    const auto names = resources.names();
    for (const auto& name : names) require(dhash_AddElement(files.get(),name.c_str(),uint32(name.size()+1)), "original asset name enters native file hash");
    for (auto name : names) {
        for (auto& c : name) { if (c >= 'a' && c <= 'z') c -= 'a'-'A'; if (c == '/') c = '\\'; }
        require(dhash_FindElement(files.get(),name.c_str(),uint32(name.size()+1)), "original asset names resolve through Windows case/separator folding");
    }
    std::cout << "native hash resolved " << names.size() << " mounted asset/resource names\n";
}
}
int main(int argc, char** argv) {
    try {
        records(); keys(); iteration(); parallel_tables();
        if (argc == 3 && std::string_view(argv[1]) == "--data") assets(argv[2]);
        std::cout << "native hash, record ownership and object-state pool tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
