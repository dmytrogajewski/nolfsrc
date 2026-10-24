#include "engine/portable/game_module.h"
#include "engine/server/class_registry.h"
#include "engine/server/counter.h"
#include "platform/system.h"
#include "platform/filesystem.h"
#include "engine/portable/memory_stream.h"
#include "engine/server/save_message.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::function<void()> lifecycle_hook;
void dispatch_lifecycle_hook() { const auto hook = lifecycle_hook; if (hook) hook(); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "invalid module operation was not rejected");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 4, "expected SDK and incompatible module paths");
        nolf::platform::Module observer(argv[1]);
        auto alive = reinterpret_cast<int (*)()>(observer.symbol("test_module_alive"));
        auto destroyed = reinterpret_cast<int (*)()>(observer.symbol("test_module_destroyed"));
        auto shells = reinterpret_cast<int (*)()>(observer.symbol("test_module_shells"));
        nolf::GameModule::Object retained;
        nolf::GameModule::Shell shell;
        {
            nolf::GameModule module(argv[1], nullptr);
            const auto log_path = std::filesystem::temp_directory_path() / ("nolf-module-log-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
            nolf::platform::set_log_file(log_path);
            reinterpret_cast<void (*)()>(observer.symbol("test_module_log"))();
            nolf::platform::set_log_file({});
            const auto log = nolf::platform::read_file(log_path);
            std::filesystem::remove(log_path);
            require(std::string(reinterpret_cast<const char*>(log.data()), log.size()) == "[module] routed through the host logger\n", "loaded module uses the host log destination");
            require(module.classes().size() >= 4, "real SDK class registration");
            auto* definition = module.find("NativeTestObject");
            require(!module.find("nativetestobject"), "class names retain SDK case-sensitive lookup");
            require(definition && definition->m_ParentClass == module.find("BaseClass"), "native class metadata and parent links");
            require(definition->m_ClassObjectAlignment == 64, "SDK reports native class alignment");
            retained = module.create(definition);
            require(alive() == 1 && reinterpret_cast<std::uintptr_t>(retained.get()) % 64 == 0, "native object aligned construction");
            require(retained.get()->EngineMessageFn(7, nullptr, 0) == 107, "cross-module SDK virtual dispatch");
            {
                nolf::MemoryStream load({},true);
                SaveMessage fixture(load,SaveMessageDirection::Write);
                require(fixture.WriteDWord(0x12345678) == LT_OK && load.SeekTo(0) == LT_OK,"prepare guarded cross-module save fixture");
                SaveMessage reader(load,SaveMessageDirection::Read);
                require(retained.get()->EngineMessageFn(MID_LOADOBJECT,static_cast<ILTMessage*>(&reader),0) == LT_OK && reader.Verify() == LT_OK,"SDK module reads through borrowed native save-message interface");
                nolf::MemoryStream saved({},true); SaveMessage writer(saved,SaveMessageDirection::Write);
                require(retained.get()->EngineMessageFn(MID_SAVEOBJECT,static_cast<ILTMessage*>(&writer),0) == LT_OK && writer.Verify() == LT_OK &&
                    std::ranges::equal(saved.data(),load.data()),"SDK module writes the original guarded scalar bytes");
            }
            shell = module.create_shell();
            require(shells() == 1 && shell.get()->ServerAppMessageFn(nullptr, 4) == 14, "native SDK server shell dispatch");
            rejects([&] { module.create(module.find("TestEditorObject")); });
            rejects([&] { module.create(module.find("ThrowingTestObject")); });
            ClassDef foreign{};
            rejects([&] { module.create(&foreign); });
            require(module.find("MissingClass") == nullptr, "missing class lookup");
            const auto saved = *definition;
            definition->m_ParentClass = definition;
            rejects([&] { nolf::GameModule invalid(argv[1], nullptr); });
            *definition = saved;
            definition->m_ClassObjectSize = 1;
            rejects([&] { nolf::GameModule invalid(argv[1], nullptr); });
            *definition = saved;
            definition->m_ClassObjectAlignment = 3;
            rejects([&] { nolf::GameModule invalid(argv[1], nullptr); });
            *definition = saved;
            const auto propertyType = definition->m_Props[0].m_PropType;
            definition->m_Props[0].m_PropType = 999;
            rejects([&] { nolf::GameModule invalid(argv[1], nullptr); });
            definition->m_Props[0].m_PropType = propertyType;
            require(retained.get()->EngineMessageFn(7, nullptr, 0) == 107, "failed validation leaves existing object intact");
        }
        require(retained.get()->EngineMessageFn(8, nullptr, 0) == 108 && alive() == 1 && shells() == 1,
                "object and shell keep their module alive");
        retained = {};
        shell = {};
        require(alive() == 0 && shells() == 0 && destroyed() == 1, "SDK object and shell destruction");
        {
            auto construction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_construction_hook"));
            auto destruction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_destruction_hook"));
            auto owner = std::make_unique<nolf::GameModule>(argv[1], nullptr);
            auto* definition = owner->find("NativeTestObject");
            lifecycle_hook = [&] { owner.reset(); };
            construction(dispatch_lifecycle_hook);
            auto instance = owner->create(definition);
            construction(nullptr); lifecycle_hook = {};
            require(!owner && instance.get() && alive() == 1, "constructor can release its module wrapper");
            instance = {};
            owner = std::make_unique<nolf::GameModule>(argv[1], nullptr);
            auto slot = owner->create(owner->find("NativeTestObject"));
            auto next = owner->create(owner->find("NativeTestObject"));
            lifecycle_hook = [&] { destruction(nullptr); lifecycle_hook = {}; slot = {}; };
            destruction(dispatch_lifecycle_hook);
            slot = std::move(next);
            require(!slot.get() && !next.get() && alive() == 0, "destructor callback mutation survives object replacement");
        }
        {
            CClassMgr first, second, third;
            first.configure(argv[1]); second.configure(argv[1]); third.configure(argv[1]);
            require(first.load(nullptr) == LT_OK && second.load(nullptr) == LT_OK, "two class registries share module definitions");
            auto* def = first.definition("NativeTestObject");
            auto* record = first.find("NativeTestObject");
            require(def && record && record->m_pClassDef == def && def == second.definition("NativeTestObject"), "canonical SDK class records");
            require(first.m_nIndex != second.m_nIndex && def->m_pInternal[first.m_nIndex] == record &&
                    def->m_pInternal[second.m_nIndex] == second.find("NativeTestObject"), "independent engine class slots");
            require(third.load(nullptr) == LT_INVALIDOBJECTDLL && third.m_nClasses == 0, "occupied class slots reject load cleanly");
            require(def->m_pInternal[first.m_nIndex] == record && second.find("NativeTestObject"), "rejected load preserves existing registries");
            auto* object = first.allocate(def);
            require(object && object->EngineMessageFn(9, nullptr, 0) == 109 && alive() == 1, "registry constructs real SDK objects");
            require(first.load(nullptr) == LT_INVALIDOBJECTDLL && alive() == 1 && first.find("NativeTestObject") == record,
                    "reload with live game objects preserves registry");
            first.destroy(object); first.destroy(object);
            require(alive() == 0, "registry releases an object once");
            require(first.allocate(def) && first.allocate(def) && alive() == 2, "registry tracks outstanding objects");
            rejects([&] { first.configure(argv[1]); });
            first.Term();
            require(alive() == 0 && shells() == 1 && !first.find("NativeTestObject"), "registry teardown releases objects and shell");
            require(def->m_pInternal[second.m_nIndex] == second.find("NativeTestObject"), "teardown preserves other engine slot");
            require(first.load(nullptr) == LT_OK, "class registry reload");
            second.Term(); first.Term();
            require(shells() == 0 && !def->m_pInternal[0] && !def->m_pInternal[1], "class slots released after reload");
        }
        {
            auto construction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_construction_hook"));
            auto destruction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_destruction_hook"));
            auto shell_construction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_shell_construction_hook"));
            auto shell_destruction = reinterpret_cast<void (*)(void (*)())>(observer.symbol("test_module_shell_destruction_hook"));
            auto registry = std::make_unique<CClassMgr>();
            registry->configure(argv[1]);
            require(registry->load(nullptr) == LT_OK, "registry callback fixture loads");
            auto* def = registry->definition("NativeTestObject");
            lifecycle_hook = [&] { registry->Term(); };
            construction(dispatch_lifecycle_hook);
            require(!registry->allocate(def), "teardown during construction rejects the retired object");
            construction(nullptr); lifecycle_hook = {};
            require(alive() == 0 && shells() == 0 && !registry->find("NativeTestObject") &&
                    !def->m_pInternal[0] && !def->m_pInternal[1], "constructor teardown releases objects, shell and class slots");
            require(registry->load(nullptr) == LT_OK, "registry reload after constructor teardown");
            lifecycle_hook = [&] { registry.reset(); };
            construction(dispatch_lifecycle_hook);
            require(!registry->allocate(def) && !registry, "constructor may destroy its class registry");
            construction(nullptr); lifecycle_hook = {};
            require(alive() == 0 && shells() == 0, "owner destruction retires the in-flight object");

            registry = std::make_unique<CClassMgr>(); registry->configure(argv[1]);
            lifecycle_hook = [&] {
                require(registry->find("NativeTestObject"), "shell constructor sees published class metadata");
                require(registry->load(nullptr) == LT_INVALIDOBJECTDLL, "nested module load is rejected");
                registry->Term();
            };
            shell_construction(dispatch_lifecycle_hook);
            require(registry->load(nullptr) == LT_INVALIDOBJECTDLL && !registry->m_pServerShell,
                    "shell constructor teardown never publishes a retired shell");
            shell_construction(nullptr); lifecycle_hook = {};
            require(shells() == 0 && !def->m_pInternal[0] && !def->m_pInternal[1], "retired shell releases its module class slots");
            lifecycle_hook = [&] { registry.reset(); };
            shell_construction(dispatch_lifecycle_hook);
            require(registry->load(nullptr) == LT_INVALIDOBJECTDLL && !registry, "shell constructor may destroy its class registry");
            shell_construction(nullptr); lifecycle_hook = {};

            for (bool during_shell : {false, true}) {
                registry = std::make_unique<CClassMgr>(); registry->configure(argv[1]);
                require(registry->load(nullptr) == LT_OK && registry->allocate(def) && registry->allocate(def), "registry destructor fixture loads");
                lifecycle_hook = [&] {
                    destruction(nullptr); shell_destruction(nullptr);
                    require(!def->m_pInternal[0] && !def->m_pInternal[1], "class slots detached before game destructor callbacks");
                    require(!registry->allocate(def) && registry->load(nullptr) == LT_INVALIDOBJECTDLL,
                            "retiring registry rejects publication from destructors");
                    registry.reset();
                };
                (during_shell ? shell_destruction : destruction)(dispatch_lifecycle_hook);
                registry->Term();
                destruction(nullptr); shell_destruction(nullptr); lifecycle_hook = {};
                require(!registry && alive() == 0 && shells() == 0, "destructor may destroy owner while all retired instances are still released");
            }
        }
        {
            CBaseCounter counter(0);
            const auto start = nolf::platform::monotonic_nanoseconds() - 2500000;
            counter.m_Data[0] = static_cast<uint32>(start); counter.m_Data[1] = static_cast<uint32>(start >> 32);
            const auto micros = counter.ReadRaw();
            require(micros >= 2500 && micros < 1000000 && counter.Read() >= 2, "SDK counter reports microseconds and milliseconds");
            LTCounter sdk{};
            sdk.m_Data[0] = counter.m_Data[0]; sdk.m_Data[1] = counter.m_Data[1];
            require(ci_EndCounter(&sdk) >= 2500, "real SDK counter storage uses microseconds");
            ci_StartCounter(&sdk);
            require(ci_EndCounter(&sdk) < 1000000 && sdk.m_Data[2] == 0 && sdk.m_Data[3] == 0, "SDK counter restart uses its own type");
            cnt_StartCounter(&counter);
            require(cnt_EndCounter(&counter) < 1000000, "SDK counter restart");
            CRateController rate;
            require(!rate.Update(), "unconfigured rate controller stays idle");
            for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                rate.SetRate(invalid);
                require(!rate.Update() && rate.GetRate() == 0, "invalid rate disabled");
            }
            rate.SetRate(1000);
            rate.m_Counter.m_Data[0] = static_cast<uint32>(start); rate.m_Counter.m_Data[1] = static_cast<uint32>(start >> 32);
            require(rate.Update() && rate.m_Accum >= 0 && rate.m_Accum < 0.001f, "rate controller preserves fractional interval");
        }
        rejects([&] { nolf::GameModule invalid(argv[2], nullptr); });
        {
            nolf::platform::Module incompatible(argv[3]);
            auto calls = reinterpret_cast<int (*)()>(incompatible.symbol("test_module_calls"));
            rejects([&] { nolf::GameModule invalid(argv[3], nullptr); });
            require(calls() == 0, "SDK ABI mismatch rejected before calling module code");
        }
        rejects([&] { nolf::GameModule missing("missing-native-game-module", nullptr); });
        std::cout << "native SDK module loading, class metadata, aligned objects, shell dispatch, and lifetime passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
