#pragma once
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace nolf::attributes {
// Case-sensitive owned symbol table for attribute tags and keys. No pointer
// packing, external lifetime, or state from a preceding lookup is required.
template<class T> class Table {
public:
    using Snapshot = std::vector<std::pair<std::string, std::shared_ptr<T>>>;
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    T* lookup(const char* name) const {
        if (!name) return nullptr;
        const auto found = entries_.find(name);
        return found == entries_.end() ? nullptr : found->second.get();
    }
    T* insert(const char* name, T* value) {
        std::shared_ptr<T> owned(value);
        if (!name || !owned) throw std::invalid_argument("Invalid attribute table entry");
        const auto [entry, inserted] = entries_.try_emplace(name, std::move(owned));
        return entry->second.get();
    }
    T* add(const char* name, T* value) { return insert(name, value); }
    void clear() { entries_.clear(); }
    Snapshot snapshot() const { return {entries_.begin(), entries_.end()}; }
    void traverse(void (*visit)(const char*, T*, void*), void* context = nullptr) const {
        const auto entries = snapshot();
        for (const auto& [name, value] : entries) visit(name.c_str(), value.get(), context);
    }
private:
    std::map<std::string, std::shared_ptr<T>, std::less<>> entries_;
};
}
