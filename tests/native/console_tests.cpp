#include "engine/server/console.h"
#include "engine/server/console_checkpoint.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/assets.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <array>
#include <climits>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
LTCommandVar* variable(ConsoleState& state, const char* name) { return reinterpret_cast<LTCommandVar*>(con_FindVar(&state,name)); }
std::string value(ConsoleState& state, const char* name) {
    auto* result = variable(state,name); require(result && result->m_pStringVal,"console variable has a value"); return result->m_pStringVal;
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        for (unsigned i = 0; i < 100; ++i) {
            path = std::filesystem::temp_directory_path() / ("nolf-console-" + std::to_string(nolf::platform::monotonic_nanoseconds()) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("console test directory allocation");
    }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
void parsers() {
    char input[] = "; first (two (three)); empty \"\"; escaped a%;b;";
    ConParse sdk(input);
    require(sdk.Parse() && sdk.m_nArgs == 2 && std::strcmp(sdk.m_Args[1],"two (three)") == 0, "SDK parser shares nested grouping and skips empty statements");
    require(sdk.Parse() && sdk.m_nArgs == 2 && !*sdk.m_Args[1], "SDK parser preserves empty quoted arguments");
    require(sdk.Parse() && std::strcmp(sdk.m_Args[1],"a;b") == 0 && !sdk.Parse() && !sdk.m_nArgs, "SDK parser shares escaped separators and EOF handling");
    CommandParser engine; engine.m_pPos = input;
    require(engine.Find("FIRST",LTFALSE,2) && std::strcmp(engine.m_ArgPointers[1],"two (three)") == 0,
            "native engine parser uses the SDK record through actual inheritance");
    sdk.Init(input); require(!sdk.ParseFind("FIRST",LTTRUE), "SDK parser find respects case sensitivity");
    sdk.Init(input); require(sdk.ParseFind("FIRST",LTFALSE,0), "SDK find accepts zero minimum as one argument");
    char nested[] = "(first 1%;second 2)";
    sdk.Init(nested); require(sdk.Parse() && sdk.m_nArgs == 1, "nested script is one grouped argument");
    sdk.Init(sdk.m_Args[0]);
    require(sdk.Parse() && std::strcmp(sdk.m_Args[0],"first") == 0 && sdk.Parse() && std::strcmp(sdk.m_Args[0],"second") == 0,
            "SDK parser can reuse its own previous argument as input without clearing unread commands");
    std::string many;
    for (unsigned i = 0; i < 64; ++i) many += "x ";
    sdk.Init(many.data()); require(sdk.Parse() && sdk.m_nArgs == 64, "SDK record supports its documented pointer count");
    many += "x; injected 1";
    sdk.Init(many.data()); require(!sdk.Parse() && !sdk.m_nArgs && !sdk.Parse(), "argument overflow cannot become a second SDK command");
    struct Guarded { uint64 before = 0x123456789abcdef0; ConParse parse; uint64 after = 0xfedcba9876543210; } guarded;
    std::string large;
    for (unsigned i = 0; i < 25; ++i) large += std::string(256,'x') + ' ';
    guarded.parse.Init(large.data());
    require(!guarded.parse.Parse() && !guarded.parse.m_nArgs && guarded.before == 0x123456789abcdef0 && guarded.after == 0xfedcba9876543210,
            "packed tokens cannot exceed the SDK's actual buffer");
    char raw[] = "name \"\"; last"; char* next = nullptr;
    std::array<char,PARSE_MAXTOKENS*PARSE_MAXTOKENSIZE> buffer{};
    std::array<char*,PARSE_MAXTOKENS> arguments{}; int count = 0;
    require(parse_Command(raw,&next,buffer.data(),arguments.data(),&count) && count == 2 && !*arguments[1], "legacy parse adapts the same lexer and reports statement continuation");
    require(!parse_Command(next,&next,buffer.data(),arguments.data(),&count) && count == 1 && std::strcmp(arguments[0],"last") == 0,
            "legacy final statement returns its tokens with continuation clear");
    nolf::CommandReader reader("a b c d e; safe 1",2);
    auto too_many = reader.next(); require(too_many && reader.overflowed(), "reader exposes a bounded statement overflow");
    auto safe = reader.next(); require(safe == std::vector<std::string>{"safe","1"} && !reader.overflowed(), "reader consumes overflowed statement remainders before the next separator");
}
unsigned creates = 0, changes = 0;
void created(ConsoleState*,LTCommandVar*) { ++creates; }
void changed(ConsoleState*,LTCommandVar*) { ++changes; }
void variables() {
    float bound_float = 0; int bound_int = 0; char* bound_text = nullptr; LTCommandVar* bound_var = nullptr;
    char name[] = "TimeScale";
    ConsoleBinding binding{name,&bound_float,&bound_int,&bound_var,&bound_text};
    ConsoleState state; state.m_pBindings = &binding; state.m_nBindings = 1;
    state.m_pOnCreate = created; state.m_pOnChange = changed;
    creates = changes = 0; require(con_Init(&state), "native console initializes");
    con_SetVar(&state,"TimeScale","2.5");
    require(creates == 1 && !changes && bound_float == 2.5f && bound_int == 2 && bound_var == variable(state,"TIMESCALE") && std::strcmp(bound_text,"2.5") == 0,
            "native variables update actual float/int/text/handle bindings and creation callbacks");
    con_SetVar(&state,"timescale","2.5"); require(!changes, "unchanged values do not notify");
    con_SetVar(&state,"timescale","AbC"); const auto* previous = bound_text;
    con_SetVar(&state,"timescale","abc");
    require(changes == 2 && std::strcmp(previous,"AbC") == 0 && std::strcmp(bound_text,"abc") == 0,
            "literal interning preserves case-only changes and prior value pointers");
    con_SetVar(&state,"timescale","1e20"); require(bound_int == INT_MAX, "large positive integer binding saturates without undefined conversion");
    con_SetVar(&state,"timescale","-1e20"); require(bound_int == INT_MIN, "large negative integer binding saturates");
    con_SetVar(&state,"timescale","nan"); require(!bound_float && !bound_int, "nonfinite config values do not enter numeric bindings");
    con_SetVar(&state,"timescale","-0x1.8p2"); require(bound_float == -6 && bound_int == -6, "numeric bindings retain hexadecimal values independently of locale");
    con_RunString(&state,"+persist 1; persist !; persist !; -persist 3; empty \"\"; bang \"!\"");
    require(value(state,"persist") == "3" && !(variable(state,"persist")->m_Flags & 1) && value(state,"empty").empty() && value(state,"bang") == "!",
            "console handles persistence prefixes, toggles, empty values and literal punctuation");
    con_RunString(&state,"toggle !; toggle !"); require(value(state,"toggle") == "0", "unquoted exclamation toggles unset and numeric variables");
    con_RunString3(&state,"blocked 7",1); require(!variable(state,"blocked"), "commands-only input cannot assign variables");
    con_Term(&state); con_Term(&state);
    require(!bound_var && !bound_text && !state.m_Variables.Size(), "console teardown clears binding handles and releases owned records");
    require(con_Init(&state), "console restarts with its configured bindings");
}
ConsoleState* active = nullptr;
ConsoleCommand* current = nullptr;
unsigned calls = 0;
void command(int count, char** arguments) {
    require(count == 1 && std::strcmp(arguments[0],"hello world") == 0, "command receives only its argument vector");
    ++calls; con_RemoveCommand(active,current);
}
void terminate(ConsoleState* state, LTCommandVar*) { con_Term(state); }
void restart(ConsoleState* state, LTCommandVar*) { state->m_pOnCreate = nullptr; con_Term(state); con_Init(state); }
void failed_command(int, char**) { active->m_Result = LT_NOTFOUND; }
void callbacks() {
    ConsoleState state; active = &state; calls = 0; con_Init(&state);
    current = con_AddCommand(&state,"say",command,nullptr);
    require(current, "native typed command registers");
    con_RunString(&state,"SAY (hello world); say ignored");
    require(calls == 1 && !state.m_Commands.Size() && value(state,"say") == "ignored", "command callbacks may unregister themselves before the next statement");
    current = con_AddCommand(&state,"say",command,nullptr);
    con_RunString3(&state,"say 17",2);
    require(calls == 1 && value(state,"say") == "17", "variables-only input cannot execute commands with the same name");
    con_SetVar(&state,"source","destroy 1; unreachable 1");
    const auto* source = variable(state,"source")->m_pStringVal;
    state.m_pOnCreate = terminate;
    con_RunString(&state,source);
    require(!state.m_Initialized && !state.m_Variables.Size(), "callbacks may terminate console-owned input without leaving a dangling lexer view");
    state.m_pOnCreate = nullptr; con_Init(&state);
    require(!variable(state,"unreachable"), "replacement console does not execute statements from the previous generation");
    state.m_pOnCreate = restart;
    con_RunString(&state,"restart 1; unreachable 2");
    require(state.m_Initialized && !variable(state,"unreachable"), "reinitialization during a callback cancels the previous input generation");
    con_AddCommand(&state,"failed",failed_command,nullptr);
    char failed[] = "failed";
    require(c_CommandHandler(&state,failed) == LT_NOTFOUND, "actual server-console entry point preserves a command handler's result");
}
void save_extra(std::string& text) { text += "+extra 42\n"; }
void fail_save(std::string&) { throw std::runtime_error("writer failure"); }
ConsoleState* saving = nullptr;
void terminate_save(std::string& text) { con_Term(saving); text += "+teardown_saved 1\n"; }
void files() {
    Directory directory;
    const auto path = directory.path / std::filesystem::u8path("настройки.cfg");
    const auto u8 = path.u8string(); const std::string name(u8.begin(),u8.end());
    ConsoleState source, restored; con_Init(&source); con_Init(&restored);
    con_SetVar(&source,"odd%;()\"name","value%;()\"\n\r!");
    variable(source,"odd%;()\"name")->m_Flags = 1;
    con_SetVar(&source,"-prefixed","!"); variable(source,"-prefixed")->m_Flags = 1;
    con_RunString(&source,"+empty \"\"; temporary 4");
    const std::string long_value(4096,'q'); con_SetVar(&source,"long",long_value.c_str()); variable(source,"long")->m_Flags = 1;
    ConsoleSaverFn writer = save_extra; source.m_pSavers = &writer; source.m_nSavers = 1;
    require(con_SaveFile(&source,name.c_str()), "native config saves atomically to a Unicode path");
    require(con_RunFile(&restored,name.c_str(),2,0), "native config loads through the platform file adapter");
    require(value(restored,"odd%;()\"name") == value(source,"odd%;()\"name") && value(restored,"-prefixed") == "!" &&
            value(restored,"empty").empty() && value(restored,"long") == long_value && value(restored,"extra") == "42" && !variable(restored,"temporary"),
            "saved settings round-trip escaped punctuation, controls, empty/long values and signed names");
    require(variable(restored,"-prefixed")->m_Flags & 1, "save markers survive reloading");
    const auto before = nolf::platform::read_file(path);
    writer = fail_save;
    require(!con_SaveFile(&source,name.c_str()) && nolf::platform::read_file(path) == before, "failed registered writer preserves the existing config file");
    require(!con_RunFile(&source,(directory.path / "missing.cfg").string().c_str(),2,0), "missing config reports failure");
    con_SetVar(&source,"destination",name.c_str()); saving = &source; writer = terminate_save;
    require(con_SaveFile(&source,variable(source,"destination")->m_pStringVal) && !source.m_Initialized &&
            con_RunFile(&restored,name.c_str(),2,0) && value(restored,"teardown_saved") == "1",
            "save snapshots retain both settings and console-owned destination across callback teardown");
}
void checkpoints() {
    ConsoleState state;
    float real = 3.5f; int integer = 12; LTCommandVar* bound = nullptr;
    char initial[] = "before binding"; char* text = initial;
    char name[] = "future"; ConsoleBinding binding{name,&real,&integer,&bound,&text};
    state.m_pBindings = &binding; state.m_nBindings = 1;
    require(con_Init(&state),"checkpoint console initialization");
    con_SetVar(&state,"existing","original"); variable(state,"existing")->m_Flags = 7;
    static int notifications = 0; notifications = 0;
    state.m_pOnCreate = state.m_pOnChange = [](ConsoleState*,LTCommandVar*) { ++notifications; };
    ConsoleCheckpoint checkpoint(state);
    con_SetVar(&state,"existing","changed"); con_SetVar(&state,"future","99"); con_SetVar(&state,"extra","new");
    const auto calls = notifications;
    require(bound && real == 99 && integer == 99 && std::string_view(text) == "99","checkpoint includes newly bound console variable");
    require(checkpoint.Restore() && value(state,"existing") == "original" && variable(state,"existing")->m_Flags == 7 &&
        !variable(state,"future") && !variable(state,"extra"),"console rollback restores values and flags and erases newly created variables");
    require(real == 3.5f && integer == 12 && !bound && text == initial && notifications == calls,"console rollback restores original bindings without callbacks");
    require(checkpoint.Restore(),"console rollback is idempotent");
    con_Term(&state); require(!checkpoint.Restore(),"console checkpoint rejects a replaced generation");
}
void original(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    std::size_t files = 0, statements = 0;
    for (const auto& name : assets.names()) {
        if (!name.ends_with(".cfg")) continue;
        const auto bytes = assets.read(name);
        std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        nolf::CommandReader reader(text,30,UINT16_MAX-1);
        while (reader.next()) { require(!reader.overflowed(), "original config statement fits native parser"); ++statements; }
        ConsoleState state; con_Init(&state); con_RunString3(&state,text.c_str(),2);
        require(state.m_Result == LT_OK, "original config variables execute through native console"); ++files;
    }
    require(files, "original config test used local data");
    std::cout << "loaded " << files << " original configs and " << statements << " statements through the native console\n";
}
}
int main(int argc, char** argv) {
    try {
        parsers(); variables(); callbacks(); files(); checkpoints();
        if (argc == 3 && std::string_view(argv[1]) == "--data") original(argv[2]);
        std::cout << "native SDK parser, console ownership and config tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
