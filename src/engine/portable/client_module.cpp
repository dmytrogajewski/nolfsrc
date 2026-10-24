#include "engine/portable/client_module.h"
#include <cstring>

namespace nolf {
struct ClientGameModule::Owner {
    platform::Module module;
    ClientModuleApi api{};
    explicit Owner(const std::filesystem::path& path) : module(path) {
        const auto& header=module.setup(sizeof(ClientModuleApi));
        std::memcpy(&api,&header,sizeof(api));
        if (api.magic != client_module_magic || api.cpp_abi != NOLF_CPP_ABI_ID || api.pointer_size != sizeof(void*) ||
            api.client_size != sizeof(ILTClient) || api.shell_size != sizeof(IClientShell) ||
            api.input_size != sizeof(platform::InputEvent) || api.shell_version != CLIENTSHELL_VERSION ||
            api.sdk_abi != sdk_abi_fingerprint || !api.create || !api.destroy || !api.input || !api.bind_log)
            throw std::runtime_error("incompatible native client module descriptor");
        api.bind_log([](const char* subsystem,std::size_t subsystem_size,const char* message,std::size_t message_size) {
            platform::log(std::string_view(subsystem,subsystem_size),std::string_view(message,message_size));
        });
    }
};
struct ClientGameModule::Shell::Instance {
    std::shared_ptr<Owner> owner;
    IClientShell* shell=nullptr;
    ~Instance() { if (shell) owner->api.destroy(shell); }
};
ClientGameModule::ClientGameModule(const std::filesystem::path& path) : owner_(std::make_shared<Owner>(path)) {}
ClientGameModule::Shell ClientGameModule::create_shell(ILTClient& engine) const {
    const auto owner=owner_;
    auto instance=std::make_shared<Shell::Instance>();
    instance->owner=owner;
    instance->shell=owner->api.create(&engine);
    if (!instance->shell) throw std::runtime_error("native client module rejected shell creation");
    Shell shell; shell.instance_=std::move(instance); return shell;
}
IClientShell* ClientGameModule::Shell::pointer(const std::shared_ptr<Instance>& instance) { return instance->shell; }
IClientShell* ClientGameModule::Shell::get() const { return instance_ ? instance_->shell : nullptr; }
void ClientGameModule::Shell::input(const platform::InputEvent& event) const {
    const auto instance=instance_;
    if (!instance) throw std::logic_error("native client shell is unavailable");
    instance->owner->api.input(instance->shell,event);
}
}
