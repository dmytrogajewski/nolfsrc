#include "console.h"
#include "dhash.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/string.h"
#include "platform/filesystem.h"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
float number(std::string_view value) {
    while (!value.empty() && static_cast<unsigned char>(value.front()) <= 32) value.remove_prefix(1);
    if (!value.empty() && value.front() == '+') value.remove_prefix(1);
    float result = 0;
    auto numeric = value;
    const bool negative = !numeric.empty() && numeric.front() == '-';
    if (negative) numeric.remove_prefix(1);
    const bool hex = numeric.starts_with("0x") || numeric.starts_with("0X");
    if (hex) numeric.remove_prefix(2); else numeric = value;
    const auto parsed = std::from_chars(numeric.data(),numeric.data()+numeric.size(),result,
                                      hex ? std::chars_format::hex : std::chars_format::general);
    if (hex && negative) result = -result;
    return parsed.ec == std::errc{} && std::isfinite(result) ? result : 0;
}
int integer(float value) {
    if (!std::isfinite(value)) return 0;
    if (double(value) >= INT_MAX) return INT_MAX;
    if (double(value) <= INT_MIN) return INT_MIN;
    return static_cast<int>(value);
}
void print(ConsoleState* state, const char* text) {
    if (state->m_pPrint) { static char format[] = "%s"; state->m_pPrint(format,text); }
}
bool assign(ConsoleState* state, const char* name, const char* value, uint32 flags, bool clear_save) {
    if (!state || !state->m_Initialized || !name || !value || !*name) return false;
    auto* text = con_InternString(state,value);
    if (!text) return false;
    auto* variable = reinterpret_cast<LTCommandVar*>(con_FindVar(state,name));
    const bool created = !variable;
    if (!variable) variable = con_CreateVar(state,name);
    if (!variable) return false;
    const bool changed = created || variable->m_pStringVal != text;
    variable->m_Flags |= flags;
    if (clear_save) variable->m_Flags &= ~1u;
    variable->m_pStringVal = text; variable->m_FloatVal = number(text);
    con_ApplyBinding(state,variable);
    // A callback may terminate or replace the console. Do not touch the
    // variable after calling it; the runner checks the state's generation.
    if (changed) if (auto callback = created ? state->m_pOnCreate : state->m_pOnChange) callback(state,variable);
    return true;
}
std::string escape_argument(std::string_view text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '%' || c == '"' || c == '(' || c == ')' || c == ';' || c == '!' || c < 32 || c == 127) result += '%';
        result += static_cast<char>(c);
    }
    result += '"'; return result;
}
}
ConsoleState::~ConsoleState() { con_Term(this); }
LTBOOL con_Init(ConsoleState* state) {
    if (!state) return LTFALSE;
    auto* variables = dhash_Create(500,DHASHKEY_STRING);
    auto* strings = dhash_Create(500,DHASHKEY_NOCASE);
    if (!variables || !strings) { dhash_Free(variables); dhash_Free(strings); return LTFALSE; }
    con_Term(state);
    state->m_hVarHash = variables; state->m_hStringHash = strings;
    state->m_Initialized = true; ++state->m_Generation;
    for (int i = 0; state->m_pBuiltins && i < state->m_nBuiltins; ++i) {
        const auto& command = state->m_pBuiltins[i];
        if (!con_AddCommand(state,command.m_pName,command.m_pFn,command.m_pUser)) { con_Term(state); return LTFALSE; }
    }
    return LTTRUE;
}
void con_Term(ConsoleState* state) {
    if (!state) return;
    const bool initialized = state->m_Initialized;
    state->m_Initialized = false; ++state->m_Generation;
    if (initialized) for (int i = 0; state->m_pBindings && i < state->m_nBindings; ++i) {
        auto& binding = state->m_pBindings[i];
        if (binding.m_ppString) *binding.m_ppString = nullptr;
        if (binding.m_ppVar) *binding.m_ppVar = nullptr;
    }
    while (state->m_CommandHead.m_pNext != &state->m_CommandHead) {
        auto* command = static_cast<ConsoleCommand*>(state->m_CommandHead.m_pNext->m_pData);
        command->m_Link.Remove(); state->m_Commands.Free(command);
    }
    state->m_Commands.Term(); state->m_Variables.Term();
    auto* variables = state->m_hVarHash; auto* strings = state->m_hStringHash;
    state->m_hVarHash = state->m_hStringHash = nullptr;
    dhash_Free(variables); dhash_Free(strings);
}
HCONSOLEVAR con_FindVar(ConsoleState* state, const char* name) {
    if (!state || !state->m_Initialized || !name || std::strlen(name) >= UINT16_MAX) return nullptr;
    return reinterpret_cast<HCONSOLEVAR>(dhash_GetData(dhash_FindElement(state->m_hVarHash,name,uint32(std::strlen(name)+1))));
}
char* con_InternString(ConsoleState* state, const char* value) {
    if (!state || !state->m_Initialized || !value || std::strlen(value) >= UINT16_MAX) return nullptr;
    const auto length = uint32(std::strlen(value)+1);
    auto* key = dhash_FindElement(state->m_hStringHash,value,length);
    if (!key) key = dhash_AddElement(state->m_hStringHash,value,length);
    return dhash_GetKey(key,nullptr);
}
LTCommandVar* con_CreateVar(ConsoleState* state, const char* name) {
    if (!state || !state->m_Initialized || !name || !*name || std::strlen(name) >= UINT16_MAX) return nullptr;
    if (auto existing = con_FindVar(state,name)) return reinterpret_cast<LTCommandVar*>(existing);
    auto* variable = state->m_Variables.Allocate();
    if (!variable) return nullptr;
    auto* key = dhash_AddElement(state->m_hVarHash,name,uint32(std::strlen(name)+1));
    if (!key) { state->m_Variables.Free(variable); return nullptr; }
    variable->m_pName = dhash_GetKey(key,nullptr); variable->m_hElement = key;
    dhash_SetData(key,variable);
    return variable;
}
ConsoleBinding* con_FindBinding(ConsoleState* state, const char* name) {
    if (state && name) for (int i = 0; state->m_pBindings && i < state->m_nBindings; ++i)
        if (state->m_pBindings[i].m_pName && nolf::ascii_compare(state->m_pBindings[i].m_pName,name) == 0) return state->m_pBindings + i;
    return nullptr;
}
void con_ApplyBinding(ConsoleState* state, LTCommandVar* variable) {
    if (!state || !state->m_Variables.Contains(variable)) return;
    if (auto* binding = con_FindBinding(state,variable->m_pName)) {
        if (binding->m_pFloat) *binding->m_pFloat = variable->m_FloatVal;
        if (binding->m_pInt) *binding->m_pInt = integer(variable->m_FloatVal);
        if (binding->m_ppVar) *binding->m_ppVar = variable;
        if (binding->m_ppString) *binding->m_ppString = variable->m_pStringVal;
    }
}
void con_SetVar(ConsoleState* state, const char* name, const char* value) { assign(state,name,value,0,false); }
ConsoleCommand* con_AddCommand(ConsoleState* state, const char* name, ConsoleCommandFn callback, void* user) {
    if (!state || !state->m_Initialized || !name || !*name || !callback) return nullptr;
    auto* command = state->m_Commands.Allocate();
    if (!command) return nullptr;
    try { command->m_Name = name; } catch (const std::bad_alloc&) { state->m_Commands.Free(command); return nullptr; }
    command->m_pName = command->m_Name.c_str(); command->m_pFn = callback; command->m_pUser = user;
    state->m_CommandHead.AddAfter(&command->m_Link);
    return command;
}
void con_RemoveCommand(ConsoleState* state, ConsoleCommand* command) {
    if (state && state->m_Commands.Contains(command)) { command->m_Link.Remove(); state->m_Commands.Free(command); }
}
void con_PrintVar(ConsoleState* state, LTCommandVar* variable) {
    if (state && state->m_Variables.Contains(variable) && variable->m_pStringVal && state->m_pPrint) {
        static char format[] = "%s = %s"; state->m_pPrint(format,variable->m_pName,variable->m_pStringVal);
    }
}
void con_RunStringEx(ConsoleState* state, const char* text, int source, int flags) {
    if (!state) return;
    state->m_Result = !text ? LT_INVALIDPARAMS : !state->m_Initialized ? LT_NOTINITIALIZED : LT_OK;
    if (state->m_Result != LT_OK) return;
    const auto generation = state->m_Generation;
    try {
        const std::string input(text);
        nolf::CommandReader reader(input,30,UINT16_MAX-1);
        while (auto arguments = reader.next()) {
            if (!state->m_Initialized || state->m_Generation != generation) break;
            if (reader.overflowed()) { state->m_Result = LT_INVALIDPARAMS; print(state,"Console statement exceeds its argument limits"); continue; }
            if (arguments->empty()) continue;
            ConsoleCommandFn callback = nullptr;
            if (!(source & 2)) for (auto* link = state->m_CommandHead.m_pNext; link != &state->m_CommandHead; link = link->m_pNext) {
                auto* command = static_cast<ConsoleCommand*>(link->m_pData);
                if (nolf::ascii_compare(command->m_pName,(*arguments)[0].c_str()) == 0) { callback = command->m_pFn; break; }
            }
            if (callback) {
                if (std::any_of(arguments->begin(),arguments->end(),[](const auto& arg) { return arg.size() > 256; })) { state->m_Result = LT_INVALIDPARAMS; continue; }
                std::vector<char*> pointers; pointers.reserve(arguments->size());
                for (auto& argument : *arguments) pointers.push_back(argument.data());
                callback(static_cast<int>(pointers.size()-1),pointers.data()+1);
            } else if (arguments->size() == 2 && !(source & 1)) {
                auto name = (*arguments)[0]; auto value = (*arguments)[1]; uint32 set = uint32(flags); bool clear = false;
                if (!name.empty() && name.front() == '+') { set |= 1; name.erase(0,1); }
                else if (!name.empty() && name.front() == '-') { clear = true; name.erase(0,1); }
                if (value == "!" && !reader.literals()[1]) {
                    auto* variable = reinterpret_cast<LTCommandVar*>(con_FindVar(state,name.c_str()));
                    if (!variable || !variable->m_pStringVal) value = "1";
                    else if ((*variable->m_pStringVal >= '0' && *variable->m_pStringVal <= '9') || *variable->m_pStringVal == '-') value = variable->m_FloatVal == 0 ? "1" : "0";
                }
                if (!assign(state,name.c_str(),value.c_str(),set,clear)) state->m_Result = LT_ERROR;
            } else if (arguments->size() == 1) con_PrintVar(state,reinterpret_cast<LTCommandVar*>(con_FindVar(state,(*arguments)[0].c_str())));
        }
    } catch (const std::bad_alloc&) { state->m_Result = LT_OUTOFMEMORY; }
    catch (const std::exception& error) { state->m_Result = LT_ERROR; print(state,error.what()); }
}
void con_RunString(ConsoleState* state, const char* text) { con_RunStringEx(state,text,0,0); }
void con_RunString3(ConsoleState* state, const char* text, int source) { con_RunStringEx(state,text,source,0); }
LTBOOL con_RunFile(ConsoleState* state, const char* name, int source, int flags) {
    if (!state || !state->m_Initialized || !name) return LTFALSE;
    try {
        const auto destination = std::filesystem::u8path(name);
        nolf::platform::InputFile file(destination);
        if (file.size() > 16 * 1024 * 1024) return LTFALSE;
        std::string text(static_cast<std::size_t>(file.size()),'\0');
        file.read_at(0,std::as_writable_bytes(std::span(text.data(),text.size())));
        con_RunStringEx(state,text.c_str(),source,flags);
        return state->m_Result == LT_OK;
    } catch (const std::exception&) { return LTFALSE; }
}
LTBOOL con_SaveFile(ConsoleState* state, const char* name) {
    if (!state || !state->m_Initialized || !name) return LTFALSE;
    try {
        const auto destination = std::filesystem::u8path(name);
        std::vector<std::pair<std::string,std::string>> variables;
        auto* iterator = dhash_GetFirst(state->m_hVarHash);
        while (auto* element = dhash_GetNext(&iterator)) {
            auto* variable = static_cast<LTCommandVar*>(dhash_GetData(element));
            if ((variable->m_Flags & 1) && variable->m_pStringVal) variables.emplace_back(variable->m_pName,variable->m_pStringVal);
        }
        std::sort(variables.begin(),variables.end());
        std::string text;
        for (const auto& [key,value] : variables) text += escape_argument("+" + key) + " " + escape_argument(value) + "\n";
        std::vector<ConsoleSaverFn> savers;
        for (int i = 0; state->m_pSavers && i < state->m_nSavers; ++i) savers.push_back(state->m_pSavers[i]);
        for (auto saver : savers) if (saver) saver(text);
        nolf::platform::atomic_write(destination,std::as_bytes(std::span(text.data(),text.size())));
        return LTTRUE;
    } catch (const std::exception&) { return LTFALSE; }
}
HCONSOLEVAR cc_FindConsoleVar(ConsoleState* state, char* name) { return con_FindVar(state,name); }
void cc_RunCommand(ConsoleState* state, char* text, int source) { con_RunString3(state,text,source); }
LTBOOL cc_LoadConfigFile(ConsoleState* state, char* name, LTBOOL source, LTBOOL flags) { return con_RunFile(state,name,int(source),int(flags)); }
LTBOOL cc_SaveConfigFile(ConsoleState* state, char* name) { return con_SaveFile(state,name); }
int cc_RunConsoleString(ConsoleState* state, char* text) {
    if (!state) return LT_INVALIDPARAMS;
    con_RunString(state,text);
    return static_cast<int>(state->m_Result);
}
LTRESULT c_CommandHandler(ConsoleState* state, char* text) {
    return static_cast<LTRESULT>(cc_RunConsoleString(state,text));
}
