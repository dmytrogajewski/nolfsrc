#include "engine/portable/game_module.h"
#include "iltmessage.h"
#include "game/Shared/TriggerMessage.h"
#include <stdexcept>

namespace {
int alive = 0, destroyed = 0, shellAlive = 0;
using EngineHook = void (*)(BaseClass*, uint32, void*, float);
EngineHook engine_hook = nullptr;
void (*construction_hook)() = nullptr;
void (*destruction_hook)() = nullptr;
void (*shell_construction_hook)() = nullptr;
void (*shell_destruction_hook)() = nullptr;
void (*shell_phase_hook)(uint32) = nullptr;
std::string last_trigger;
BaseClass* enter_object = nullptr;
}
DEFINE_CLASSES()
class alignas(64) NativeTestObject : public BaseClass {
public:
    NativeTestObject() { ++alive; if (construction_hook) construction_hook(); }
    ~NativeTestObject() override { --alive; ++destroyed; if (destruction_hook) destruction_hook(); }
    uint32 EngineMessageFn(uint32 message, void* data, float parameter) override {
        if (engine_hook) engine_hook(this,message,data,parameter);
        if (data && message == MID_SAVEOBJECT) return static_cast<ILTMessage*>(data)->WriteDWord(saved_value);
        if (data && message == MID_LOADOBJECT) return static_cast<ILTMessage*>(data)->ReadDWordFL(saved_value);
        return message + 100;
    }
    uint32 saved_value = 42;
    uint32 ObjectMessageFn(HOBJECT, uint32 id, HMESSAGEREAD message) override {
        if (id != 1000) return 0;
        const auto text = ReadTriggerMessage(*g_pLTServer, message);
        g_pLTServer->ResetRead(message);
        if (ReadTriggerMessage(*g_pLTServer, message) != text)
            throw std::runtime_error("Inherited trigger reader differs");
        if (engine_hook) engine_hook(this, id, const_cast<char*>(text.c_str()), 0);
        last_trigger = text;
        return 1;
    }
};
BEGIN_CLASS(NativeTestObject)
    ADD_LONGINTPROP(Value, 42)
END_CLASS_DEFAULT(NativeTestObject, BaseClass, nullptr, nullptr)
class TestEditorObject : public BaseClass {};
BEGIN_CLASS(TestEditorObject)
END_CLASS_DEFAULT_FLAGS(TestEditorObject, BaseClass, nullptr, nullptr, CF_NORUNTIME)
class ThrowingTestObject : public BaseClass {
public:
    ThrowingTestObject() { throw std::runtime_error("intentional game constructor failure"); }
};
BEGIN_CLASS(ThrowingTestObject)
END_CLASS_DEFAULT(ThrowingTestObject, BaseClass, nullptr, nullptr)
class RefusingTestObject : public BaseClass {
public:
    uint32 EngineMessageFn(uint32 message, void*, float) override { return message == MID_PRECREATE ? 0 : 1; }
};
BEGIN_CLASS(RefusingTestObject)
END_CLASS_DEFAULT(RefusingTestObject, BaseClass, nullptr, nullptr)
class NativeTestShell final : public IServerShell {
public:
    NativeTestShell() { ++shellAlive; if (shell_construction_hook) shell_construction_hook(); }
    ~NativeTestShell() override { --shellAlive; if (shell_destruction_hook) shell_destruction_hook(); }
    LPBASECLASS OnClientEnterWorld(HCLIENT, void*, uint32) override {
        auto* result = enter_object;
        if (shell_phase_hook) shell_phase_hook(7);
        return result;
    }
    LTRESULT ServerAppMessageFn(char*, int length) override { return uint32(length + 10); }
    void SRand(unsigned seed) override { IServerShell::SRand(seed); if (shell_phase_hook) shell_phase_hook(0); }
    void PreStartWorld(LTBOOL) override { if (shell_phase_hook) shell_phase_hook(1); }
    void CacheFiles() override { if (shell_phase_hook) shell_phase_hook(2); }
    void PostStartWorld() override { if (shell_phase_hook) shell_phase_hook(3); }
    void Update(float) override { if (shell_phase_hook) shell_phase_hook(4); }
    void OnCommandOn(HCLIENT,int) override { if (shell_phase_hook) shell_phase_hook(5); }
    void OnCommandOff(HCLIENT,int) override { if (shell_phase_hook) shell_phase_hook(6); }
};
IServerShell* CreateServerShell(ILTServer*) { return new NativeTestShell; }
void DeleteServerShell(IServerShell* shell) { delete shell; }
extern "C" NOLF_EXPORT int test_module_alive() { return alive; }
extern "C" NOLF_EXPORT int test_module_destroyed() { return destroyed; }
extern "C" NOLF_EXPORT int test_module_shells() { return shellAlive; }
extern "C" NOLF_EXPORT void test_module_hook(EngineHook hook) { engine_hook = hook; }
extern "C" NOLF_EXPORT uint32 test_module_value(BaseClass* object) { return static_cast<NativeTestObject*>(object)->saved_value; }
extern "C" NOLF_EXPORT void test_module_set_value(BaseClass* object, uint32 value) { static_cast<NativeTestObject*>(object)->saved_value = value; }
extern "C" NOLF_EXPORT const char* test_module_trigger() { return last_trigger.c_str(); }
extern "C" NOLF_EXPORT void test_module_construction_hook(void (*hook)()) { construction_hook = hook; }
extern "C" NOLF_EXPORT void test_module_destruction_hook(void (*hook)()) { destruction_hook = hook; }
extern "C" NOLF_EXPORT void test_module_shell_construction_hook(void (*hook)()) { shell_construction_hook = hook; }
extern "C" NOLF_EXPORT void test_module_shell_destruction_hook(void (*hook)()) { shell_destruction_hook = hook; }
extern "C" NOLF_EXPORT void test_module_shell_phase_hook(void (*hook)(uint32)) { shell_phase_hook = hook; }
extern "C" NOLF_EXPORT void test_module_enter_object(BaseClass* object) { enter_object = object; }
extern "C" NOLF_EXPORT void test_module_log() { nolf::platform::log("module", "routed through the host logger"); }
