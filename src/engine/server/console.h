#pragma once
#include "ltbasedefs.h"
#include "ltlink.h"
#include "engine_config.h"
#include "engine/portable/record_pool.h"
#include "command_parser.h"
#include <string>

class CServerMgr;
class ConsoleState;
using ConsoleCommandFn = void(*)(int, char**);
using ConsoleSaverFn = void(*)(std::string&);
class LTCommandVar {
public:
    char* m_pName = nullptr;
    char* m_pStringVal = nullptr;
    float m_FloatVal = 0;
    void* m_hElement = nullptr;
    uint32 m_Flags = 0;
};
struct ConsoleCommandDef { const char* m_pName; ConsoleCommandFn m_pFn; void* m_pUser; };
class ConsoleCommand {
public:
    ConsoleCommand() { m_Link.Init2(this); }
    const char* m_pName = nullptr;
    ConsoleCommandFn m_pFn = nullptr;
    LTLink m_Link;
    void* m_pUser = nullptr;
    std::string m_Name;
};
class ConsoleState {
public:
    ConsoleState() { m_CommandHead.Init2(nullptr); }
    ~ConsoleState();
    ConsoleState(const ConsoleState&) = delete;
    ConsoleState& operator=(const ConsoleState&) = delete;
    ConsoleSaverFn* m_pSavers = nullptr;
    int m_nSavers = 0;
    ConsoleBinding* m_pBindings = nullptr;
    int m_nBindings = 0;
    ConsoleCommandDef* m_pBuiltins = nullptr;
    int m_nBuiltins = 0;
    LTLink m_CommandHead;
    void (*m_pPrint)(char*, ...) = nullptr;
    void (*m_pOnCreate)(ConsoleState*,LTCommandVar*) = nullptr;
    void (*m_pOnChange)(ConsoleState*,LTCommandVar*) = nullptr;
    CServerMgr* m_pServerMgr = nullptr;
    void* m_hVarHash = nullptr;
    void* m_hStringHash = nullptr;
    nolf::RecordPool<LTCommandVar> m_Variables;
    nolf::RecordPool<ConsoleCommand> m_Commands;
    uint64 m_Generation = 0;
    LTRESULT m_Result = LT_OK;
    bool m_Initialized = false;
};
LTBOOL con_Init(ConsoleState*);
void con_Term(ConsoleState*);
HCONSOLEVAR con_FindVar(ConsoleState*,const char*);
char* con_InternString(ConsoleState*,const char*);
LTCommandVar* con_CreateVar(ConsoleState*,const char*);
void con_SetVar(ConsoleState*,const char*,const char*);
ConsoleBinding* con_FindBinding(ConsoleState*,const char*);
void con_ApplyBinding(ConsoleState*,LTCommandVar*);
ConsoleCommand* con_AddCommand(ConsoleState*,const char*,ConsoleCommandFn,void*);
void con_RemoveCommand(ConsoleState*,ConsoleCommand*);
void con_PrintVar(ConsoleState*,LTCommandVar*);
void con_RunString(ConsoleState*,const char*);
void con_RunString3(ConsoleState*,const char*,int);
void con_RunStringEx(ConsoleState*,const char*,int,int);
LTBOOL con_RunFile(ConsoleState*,const char*,int,int);
LTBOOL con_SaveFile(ConsoleState*,const char*);
HCONSOLEVAR cc_FindConsoleVar(ConsoleState*,char*);
void cc_RunCommand(ConsoleState*,char*,int);
LTBOOL cc_LoadConfigFile(ConsoleState*,char*,LTBOOL,LTBOOL);
LTBOOL cc_SaveConfigFile(ConsoleState*,char*);
int cc_RunConsoleString(ConsoleState*,char*);
LTRESULT c_CommandHandler(ConsoleState*,char*);
