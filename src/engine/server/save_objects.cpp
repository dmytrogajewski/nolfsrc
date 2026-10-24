#include "save_objects.h"
#include <new>
#include <stdexcept>

SaveObjectReferences::SaveObjectReferences(std::span<LTObject* const> objects, std::optional<CPosRef> position, Formatter formatter)
    : position_(std::move(position)), formatter_(std::move(formatter)) {
    if (objects.size() > 0xffff) throw std::invalid_argument("too many saved object references");
    entries_.reserve(objects.size()); indices_.reserve(objects.size());
    for (auto* object : objects) {
        if (!object || !indices_.emplace(object,static_cast<uint16>(entries_.size())).second) throw std::invalid_argument("invalid saved object reference table");
        entries_.push_back({object,ObjectCallbackGuard(*object)});
    }
}
LTRESULT SaveObjectReferences::WriteObject(LMessageImpl* message, HOBJECT object) {
    if (!message) return LT_INVALIDPARAMS;
    const auto found = indices_.find(object);
    const auto index = found != indices_.end() && entries_[found->second].guard.unchanged() ? found->second : uint16(0xffff);
    return message->WriteWord(index);
}
LTRESULT SaveObjectReferences::ReadObject(LMessageImpl* message, HOBJECT* output) {
    if (!message || !output) return LT_INVALIDPARAMS;
    *output = nullptr;
    uint16 index = 0xffff;
    const auto result = message->ReadWordFL(index);
    if (result != LT_OK || index == 0xffff) return result;
    if (index >= entries_.size()) return message->FlagError(LMSTAT_READOVERFLOW,LT_INVALIDFILE);
    const auto& entry = entries_[index];
    if (entry.guard.unchanged()) *output = entry.object;
    return LT_OK;
}
LTRESULT SaveObjectReferences::WriteFormattedHString(LMessageImpl* message, int code, va_list* arguments) {
    if (!message) return LT_INVALIDPARAMS;
    if (!formatter_) return message->FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINITIALIZED);
    try {
        auto text = formatter_(code,arguments);
        return message->WriteStringAsHString(text.data());
    } catch (const std::bad_alloc&) { return message->FlagError(LMSTAT_WRITEOVERFLOW,LT_OUTOFMEMORY); }
      catch (const std::exception&) { return message->FlagError(LMSTAT_WRITEOVERFLOW,LT_ERROR); }
}
void SaveObjectReferences::GetPosRef(CPosRef** output) {
    if (output) *output = position_ ? &*position_ : nullptr;
}
