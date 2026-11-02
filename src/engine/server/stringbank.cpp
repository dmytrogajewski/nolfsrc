#include "stringbank.h"
#include "engine/portable/string.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include <thread>

namespace {
struct StringRecord {
    explicit StringRecord(const char* text) : value(text) {}
    std::atomic<uint32> references{1};
    std::string value;
    std::thread::id creator = std::this_thread::get_id();
};
std::mutex bankMutex;
std::unordered_set<StringRecord*> strings;
StringRecord emptyString("");
StringRecord* record(HSTRING handle) { return reinterpret_cast<StringRecord*>(handle); }
}
char g_EmptyString[] = "";

HSTRING sb_CreateString(char* text) {
    if (!text) return nullptr;
    if (!*text) return reinterpret_cast<HSTRING>(&emptyString);
    auto* item = new StringRecord(text);
    std::lock_guard lock(bankMutex);
    strings.insert(item);
    return reinterpret_cast<HSTRING>(item);
}
HSTRING sb_CreateStringRaw(char* text) { return sb_CreateString(text); }
HSTRING sb_CopyString(HSTRING handle) {
    if (handle && record(handle) != &emptyString) ++record(handle)->references;
    return handle;
}
void sb_ReleaseString(HSTRING handle) {
    if (!handle || record(handle) == &emptyString) return;
    auto* item = record(handle);
    std::lock_guard lock(bankMutex);
    if (--item->references == 0) { strings.erase(item); delete item; }
}
void sb_FreeString(HSTRING handle) { sb_ReleaseString(handle); }
char* sb_GetStringData(HSTRING handle) {
    return handle ? record(handle)->value.data() : nullptr;
}
std::optional<std::string> sb_GetStringSnapshot(HSTRING handle) {
    std::lock_guard lock(bankMutex); const auto* item=record(handle);
    if (item==&emptyString) return std::string{};
    if (!item || !strings.contains(const_cast<StringRecord*>(item))) return {};
    return item->value;
}
char* sb_GetStringDataAndLen(HSTRING handle, uint32* length) {
    if (length) *length = handle ? uint32(record(handle)->value.size() + 1) : 0;
    return sb_GetStringData(handle);
}
LTBOOL sb_CompareStrings(HSTRING a, HSTRING b) {
    return a && b && record(a)->value == record(b)->value;
}
LTBOOL sb_CompareStringsUpper(HSTRING a, HSTRING b) {
    return a && b && nolf::ascii_compare(record(a)->value.c_str(), record(b)->value.c_str()) == 0;
}
void sb_EnumStrings(void (*callback)(char*, void*), void* user) {
    if (!callback) return;
    std::vector<std::string> snapshot;
    {
        std::lock_guard lock(bankMutex);
        for (const auto* item : strings) snapshot.push_back(item->value);
    }
    for (auto& text : snapshot) callback(text.data(), user);
}
void sb_EnumThreadStrings(void (*callback)(char*, void*), void* user) {
    if (!callback) return;
    std::vector<std::string> snapshot;
    {
        std::lock_guard lock(bankMutex);
        for (const auto* item : strings)
            if (item->creator == std::this_thread::get_id()) snapshot.push_back(item->value);
    }
    for (auto& text : snapshot) callback(text.data(), user);
}
