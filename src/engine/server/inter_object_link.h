#pragma once
#include "ltobject_internal.h"
#include <map>
#include <memory>

class SoundInstance;
constexpr uint32 INTERLINK_CONTAINER = 1;
struct InterObjectLink {
    uint32 m_Type = 0;
    LTObject* m_pOwner = nullptr;
    LTObject* m_pLinked = nullptr;
    SoundInstance* sound = nullptr;
    LTLink owner_link, linked_link;
    InterObjectLink() { owner_link.Init2(this); linked_link.Init2(this); }
};

// Owns both endpoint links and the record. A sound is a separate endpoint
// type; no sound pointer is ever interpreted as an LTObject.
class InterObjectLinks {
public:
    ~InterObjectLinks() { Clear(); }
    bool Create(LTObject*, LTObject*, uint32 type);
    bool Attach(LTObject*, SoundInstance*);
    void Break(LTObject* owner, LTObject* linked, bool all);
    void Detach(LTObject* owner, SoundInstance*, bool all);
    using Notify = void (*)(void*, LTObject* owner, LTObject* linked);
    void BreakFor(LTObject*, uint32 type, Notify = nullptr, void* context = nullptr);
    bool VisitContainers(LTObject*, bool (*visitor)(void*, LTObject*), void* context);
    void Clear();
    std::size_t Size() const { return records_.size(); }
private:
    std::map<std::uint64_t, std::unique_ptr<InterObjectLink>> records_;
    std::uint64_t next_id_ = 1;
    using Iterator = decltype(records_)::iterator;
    void Erase(Iterator, bool kill_sound);
};
