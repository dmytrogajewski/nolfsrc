#pragma once
#include "saveio.h"
#include "engine/portable/saved_game.h"
#include "server_lifetime.h"
#include <stdexcept>

namespace nolf::save {
inline constexpr uint32 byte_budget = 256u*1024*1024;
class Failure : public std::runtime_error {
public:
    Failure(LTRESULT result, const char* text) : std::runtime_error(text), code(result) {}
    LTRESULT code;
};
inline void require(bool condition, LTRESULT code, const char* text) { if (!condition) throw Failure(code,text); }
SavedGame capture(CServerMgr&, const ObjectList&, uint32 parameter, uint32 flags);
SavedGame read(ILTStream&);
LTRESULT apply(CServerMgr&, const SavedGame&, uint32 parameter, uint32 flags);
}
