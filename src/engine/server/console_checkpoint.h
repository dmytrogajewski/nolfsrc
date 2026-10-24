#pragma once
#include "console.h"
#include <unordered_set>
#include <vector>

// A console transaction retains interned values while its generation lives.
// Rollback allocates nothing and invokes no user callbacks.
class ConsoleCheckpoint {
public:
    explicit ConsoleCheckpoint(ConsoleState&);
    bool Restore();
private:
    struct Variable { LTCommandVar* pointer; LTCommandVar value; };
    struct Binding { ConsoleBinding target; float real = 0; int integer = 0; LTCommandVar* variable = nullptr; char* text = nullptr; };
    ConsoleState& state_;
    uint64 generation_;
    ConsoleBinding* bindings_;
    int binding_count_;
    std::vector<Variable> variables_;
    std::unordered_set<LTCommandVar*> original_;
    std::vector<Binding> binding_values_;
};
