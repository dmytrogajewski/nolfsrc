#pragma once
#include "ltbasedefs.h"

// Native engine convenience names refer to the SDK record's own storage.
// SDK callers use ConParse directly; no layout cast is involved.
class CommandParser : public ConParse {
public:
    CommandParser() = default;
    CommandParser(const CommandParser&) = delete;
    CommandParser& operator=(const CommandParser&) = delete;
    int Next() { return Parse(); }
    LTBOOL Find(const char* name, LTBOOL sensitive, int count) { return ParseFind(name,sensitive,count > 0 ? uint32(count) : 1); }
    char* (&m_ArgPointers)[PARSE_MAXTOKENS] = m_Args;
    char*& m_pPos = CommandPosition();
};
int parse_Command(char*, char**, char*, char**, int*);
