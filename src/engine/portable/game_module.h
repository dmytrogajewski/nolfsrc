#pragma once
#include "platform/compiler.h"
#include "platform/module.h"
#include "platform/logging.h"
#include "ltserverobj.h"
#include "ltengineobjects.h"
#include "iservershell.h"
#include "nolf/sdk_abi.h"
#include <span>
#include <string_view>

namespace nolf {
using ObjectSetup = ClassDef** (*)(int*, ILTServer*, int*);
inline constexpr uint32 server_module_magic = 0x53464c4e; // NLFS
struct ServerModuleApi {
    platform::ModuleHeader header;
    uint32 magic, cpp_abi, pointer_size, class_size, property_size, base_size;
    uint32 object_version, shell_version;
    uint64 sdk_abi;
    ObjectSetup objects;
    CreateServerShellFn create_shell;
    DeleteServerShellFn delete_shell;
    void (*bind_log)(platform::LogSink);
};
constexpr ServerModuleApi server_module_api(ObjectSetup setup, CreateServerShellFn create, DeleteServerShellFn destroy,
        void (*bind_log)(platform::LogSink) = nullptr) {
    return {{platform::module_api_version, sizeof(ServerModuleApi)}, server_module_magic,
        NOLF_CPP_ABI_ID, sizeof(void*), sizeof(ClassDef), sizeof(PropDef), sizeof(BaseClass),
        SERVEROBJ_VERSION, SERVERSHELL_VERSION, sdk_abi_fingerprint, setup, create, destroy, bind_log};
}

// Loading, SDK class validation, and instance lifetime are separate from the
// server's world attachment and profiling records. Instances retain the module
// that supplies their constructors, virtual methods, and destructors.
class GameModule {
public:
    explicit GameModule(const std::filesystem::path&, ILTServer*);
    class Object {
    public:
        Object() = default;
        ~Object();
        Object(Object&&) noexcept;
        Object& operator=(Object&&) noexcept;
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;
        BaseClass* get() const { return pointer_; }
    private:
        friend class GameModule;
        struct Owner;
        std::shared_ptr<Owner> owner_;
        BaseClass* pointer_ = nullptr;
        ClassDef* definition_ = nullptr;
        void reset();
    };
    class Shell {
    public:
        Shell() = default;
        ~Shell();
        Shell(Shell&&) noexcept;
        Shell& operator=(Shell&&) noexcept;
        Shell(const Shell&) = delete;
        Shell& operator=(const Shell&) = delete;
        IServerShell* get() const { return pointer_; }
    private:
        friend class GameModule;
        std::shared_ptr<Object::Owner> owner_;
        IServerShell* pointer_ = nullptr;
        void reset();
    };
    std::span<ClassDef* const> classes() const;
    ClassDef* find(std::string_view) const;
    Object create(ClassDef*) const;
    Shell create_shell() const;
    // A callback may destroy its last object or terminate the server. Keep
    // the supplying module mapped until control has returned to the engine.
    std::shared_ptr<const void> lease() const { return owner_; }
private:
    std::shared_ptr<Object::Owner> owner_;
};
}
