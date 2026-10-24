#pragma once
#include "iltserver.h"
#include "object_lifetime.h"
#include <string>
#include <string_view>

// Restored player objects remain available to a reconnecting client through
// opaque SDK references. The table owns records and names; identity is the
// actual object lifetime, rather than a possibly recycled numeric object ID.
class ClientReferences {
public:
    struct Entry {
        Entry(LTObject& value, uint32 options, std::string text)
            : object(&value), guard(value), flags(options), name(std::move(text)) { link.Init2(this); }
        LTLink link;
        LTObject* object;
        ObjectCallbackGuard guard;
        uint32 flags;
        std::string name;
    };
    ~ClientReferences() { Clear(); }
    ClientReferences() = default;
    ClientReferences(const ClientReferences&) = delete;
    ClientReferences& operator=(const ClientReferences&) = delete;
    HCLIENTREF Add(LTObject&, uint32 flags, std::string name);
    void RemoveFor(LTObject*);
    void Clear();
    const Entry* Find(HCLIENTREF) const;
    HCLIENTREF Next(HCLIENTREF) const;
    bool CopyName(HCLIENTREF, char*, int capacity) const;
    std::size_t Size() const { return list_.m_nElements; }
private:
    LTList list_;
};
