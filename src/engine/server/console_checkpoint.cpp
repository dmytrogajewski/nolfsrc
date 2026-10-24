#include "console_checkpoint.h"
#include "dhash.h"

ConsoleCheckpoint::ConsoleCheckpoint(ConsoleState& state) : state_(state), generation_(state.m_Generation),
    bindings_(state.m_pBindings), binding_count_(state.m_nBindings) {
    for (void* iterator = dhash_GetFirst(state.m_hVarHash); iterator;) {
        auto* value = static_cast<LTCommandVar*>(dhash_GetData(dhash_GetNext(&iterator)));
        variables_.push_back({value,*value}); original_.insert(value);
    }
    for (int i = 0; bindings_ && i < binding_count_; ++i) {
        Binding snapshot; snapshot.target = bindings_[i];
        if (snapshot.target.m_pFloat) snapshot.real = *snapshot.target.m_pFloat;
        if (snapshot.target.m_pInt) snapshot.integer = *snapshot.target.m_pInt;
        if (snapshot.target.m_ppVar) snapshot.variable = *snapshot.target.m_ppVar;
        if (snapshot.target.m_ppString) snapshot.text = *snapshot.target.m_ppString;
        binding_values_.push_back(snapshot);
    }
}
bool ConsoleCheckpoint::Restore() {
    if (!state_.m_Initialized || state_.m_Generation != generation_ || state_.m_pBindings != bindings_ || state_.m_nBindings != binding_count_) return false;
    for (const auto& variable : variables_)
        if (!state_.m_Variables.Contains(variable.pointer) || variable.pointer->m_hElement != variable.value.m_hElement) return false;
    for (void* iterator = dhash_GetFirst(state_.m_hVarHash); iterator;) {
        auto* element = dhash_GetNext(&iterator);
        auto* variable = static_cast<LTCommandVar*>(dhash_GetData(element));
        if (!original_.contains(variable)) { dhash_Remove(state_.m_hVarHash,element); state_.m_Variables.Free(variable); }
    }
    for (const auto& variable : variables_) *variable.pointer = variable.value;
    for (const auto& binding : binding_values_) {
        if (binding.target.m_pFloat) *binding.target.m_pFloat = binding.real;
        if (binding.target.m_pInt) *binding.target.m_pInt = binding.integer;
        if (binding.target.m_ppVar) *binding.target.m_ppVar = binding.variable;
        if (binding.target.m_ppString) *binding.target.m_ppString = binding.text;
    }
    return true;
}
