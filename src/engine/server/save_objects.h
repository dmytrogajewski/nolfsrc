#pragma once
#include "message.h"
#include "object_lifetime.h"
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Each save operation owns its reference table. Recursive saves and restores
// never share an object-index global or overwrite native object IDs.
class SaveObjectReferences final : public CMsgObjectIO {
public:
    using Formatter = std::function<std::string(int,va_list*)>;
    explicit SaveObjectReferences(std::span<LTObject* const>, std::optional<CPosRef> = {}, Formatter = {});
    LTRESULT ReadObject(LMessageImpl*, HOBJECT*) override;
    LTRESULT WriteObject(LMessageImpl*, HOBJECT) override;
    LTRESULT WriteFormattedHString(LMessageImpl*, int, va_list*) override;
    void GetPosRef(CPosRef**) override;
private:
    struct Entry { LTObject* object; ObjectCallbackGuard guard; };
    std::vector<Entry> entries_;
    std::unordered_map<LTObject*,uint16> indices_;
    std::optional<CPosRef> position_;
    Formatter formatter_;
};
