#pragma once
#include "platform/compiler.h"
#include "platform/module.h"
#include "platform/logging.h"
#include "platform/window.h"
#include "iltclient.h"
#include "nolf/sdk_abi.h"
#include <stdexcept>
#include <utility>

namespace nolf {
inline constexpr uint32 client_module_magic=0x43464c4e; // NLFC
using ClientInputFn=void (*)(IClientShell*,const platform::InputEvent&);
struct ClientModuleApi {
    platform::ModuleHeader header;
    uint32 magic, cpp_abi, pointer_size, client_size, shell_size, input_size, shell_version;
    uint64 sdk_abi;
    CreateClientShellFn create;
    DeleteClientShellFn destroy;
    ClientInputFn input;
    void (*bind_log)(platform::LogSink);
};
constexpr ClientModuleApi client_module_api(CreateClientShellFn create,DeleteClientShellFn destroy,ClientInputFn input,
        void (*bind_log)(platform::LogSink)) {
    return {{platform::module_api_version,sizeof(ClientModuleApi)},client_module_magic,NOLF_CPP_ABI_ID,
        sizeof(void*),sizeof(ILTClient),sizeof(IClientShell),sizeof(platform::InputEvent),CLIENTSHELL_VERSION,
        sdk_abi_fingerprint,create,destroy,input,bind_log};
}

// The engine/window services outlive every shell. Callback leases also keep
// the actual game instance alive when a callback retires its owning host.
class ClientGameModule {
    struct Owner;
public:
    explicit ClientGameModule(const std::filesystem::path&);
    class Shell {
        struct Instance;
    public:
        Shell()=default;
        Shell(Shell&&) noexcept=default;
        Shell& operator=(Shell&&) noexcept=default;
        Shell(const Shell&)=delete;
        Shell& operator=(const Shell&)=delete;
        IClientShell* get() const;
        void input(const platform::InputEvent&) const;
        template<class Callback> decltype(auto) call(Callback&& callback) const {
            const auto instance=instance_;
            if (!instance) throw std::logic_error("native client shell is unavailable");
            return std::forward<Callback>(callback)(*pointer(instance));
        }
    private:
        friend class ClientGameModule;
        std::shared_ptr<Instance> instance_;
        static IClientShell* pointer(const std::shared_ptr<Instance>&);
    };
    Shell create_shell(ILTClient&) const;
    std::shared_ptr<const void> lease() const { return owner_; }
private:
    std::shared_ptr<Owner> owner_;
};
}
