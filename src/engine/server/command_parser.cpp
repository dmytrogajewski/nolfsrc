#include "command_parser.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/string.h"
#include <cstring>

namespace {
bool pack(const std::vector<std::string>& arguments, char* buffer, std::size_t capacity, char** pointers, int& count) {
    std::size_t size = 0;
    for (const auto& argument : arguments) {
        if (argument.size() + 1 > capacity - size) return false;
        size += argument.size() + 1;
    }
    for (const auto& argument : arguments) {
        pointers[count++] = buffer;
        std::memcpy(buffer,argument.c_str(),argument.size() + 1);
        buffer += argument.size() + 1;
    }
    return true;
}
}
LTBOOL ConParse::Parse() {
    m_nArgs = 0; m_Args[0] = m_ArgBuffer;
    if (!m_pCommandPos) { m_ArgBuffer[0] = 0; return LTFALSE; }
    try {
        nolf::CommandReader reader(m_pCommandPos,PARSE_MAXTOKENS);
        while (auto arguments = reader.next()) {
            if (reader.overflowed() || !pack(*arguments,m_ArgBuffer,sizeof(m_ArgBuffer),m_Args,m_nArgs)) {
                m_nArgs = 0; m_ArgBuffer[0] = 0; m_pCommandPos = nullptr; return LTFALSE;
            }
            if (m_nArgs) { m_pCommandPos += reader.position(); return LTTRUE; }
        }
    } catch (const std::bad_alloc&) {}
    m_ArgBuffer[0] = 0;
    m_pCommandPos = nullptr;
    return LTFALSE;
}
LTBOOL ConParse::ParseFind(const char* name, LTBOOL sensitive, uint32 minimum) {
    if (!name) return LTFALSE;
    if (!minimum) minimum = 1;
    while (Parse()) if (uint32(m_nArgs) >= minimum &&
        (sensitive ? std::strcmp(m_Args[0],name) == 0 : nolf::ascii_compare(m_Args[0],name) == 0)) return LTTRUE;
    return LTFALSE;
}
int parse_Command(char* input, char** next, char* buffer, char** arguments, int* count) {
    if (count) *count = 0;
    if (next) *next = nullptr;
    if (!input || !next || !buffer || !arguments || !count) return 0;
    arguments[0] = buffer;
    try {
        nolf::CommandReader reader(input,PARSE_MAXARGS);
        const auto statement = reader.next();
        if (!statement || reader.overflowed() ||
            !pack(*statement,buffer,PARSE_MAXTOKENS * PARSE_MAXTOKENSIZE,arguments,*count)) { *count = 0; buffer[0] = 0; return 0; }
        if (!*count) buffer[0] = 0;
        *next = input + reader.position();
        return reader.ended_statement();
    } catch (const std::bad_alloc&) { return 0; }
}
