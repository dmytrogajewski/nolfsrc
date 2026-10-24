#define __SERVERAPI_EXPORT__
#include "engine/client/local_server.h"
#include "engine/server/servermgr.h"
#include "engine/server/server_access.h"
#include "engine/portable/engine_resources.h"
#include "game/Shared/NetDefs.h"
#include "platform/timing.h"
#include <atomic>
#include <future>
#include <tuple>
#include <random>
#include <stdexcept>

namespace nolf {
namespace {
void command(ServerInterface& server,std::string text) {
    if (server.RunConsoleString(text.data())!=LT_OK) throw std::runtime_error("local server command failed: "+text);
}
std::atomic<bool> running{false};
}
struct LocalServer::State {
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::exception_ptr failure;
    std::thread thread;
};
LocalServer::LocalServer(const std::filesystem::path& data,const std::filesystem::path& module,
        std::shared_ptr<const Resources> resources,std::span<const std::byte> game_info) : state_(std::make_shared<State>()) {
    if (running.exchange(true)) throw std::logic_error("a local server is already running");
    std::promise<std::pair<std::string,CBaseDriver*>> started; auto address=started.get_future();
    std::vector<std::byte> info(game_info.begin(),game_info.end());
    try {
        state_->thread=std::thread([state=state_,data,module,resources,info=std::move(info),started=std::move(started)]() mutable {
            ServerInterface* server=nullptr; bool announced=false;
            try {
                configure_engine_resources(resources);
                auto guid=kNolfApplicationGuid;
                if (CreateServer(SI_VERSION,guid,&server)!=SI_OK || !server) { server=nullptr; throw std::runtime_error("local server creation failed"); }
                {
                    ServerInterfaceCall access;
                    if (!access.available()) throw std::runtime_error("local server retired during startup");
                    auto& manager=*access.manager();
                    manager.m_FileMgr.mount_game(data);
                    manager.m_ClassMgr.configure(module,{resources.get(),"cres",1033});
                }
                command(*server,"MasterServer none");
                command(*server,"NetMaxPlayers 1");
                command(*server,"BindIP 127.0.0.1");
                if (!info.empty() && !server->SetGameInfo(info.data(),uint32(info.size()))) throw std::runtime_error("local server rejected the game information");
                if (!server->InitNetworking(nullptr,0)) throw std::runtime_error("local server networking failed");
                NetService* services=nullptr;
                if (!server->GetServiceList(services)) throw std::runtime_error("local server service enumeration failed");
                HNETSERVICE internet=nullptr;
                for (auto* service=services;service;service=service->m_pNext) if (service->m_dwFlags&NETSERVICE_TCPIP) { internet=service->m_handle; break; }
                server->FreeServiceList(services);
                if (!internet || !server->SelectService(internet)) throw std::runtime_error("local server transport unavailable");
                // An unused loopback port; the default belongs to dedicated servers.
                std::mt19937 random(std::random_device{}()); bool hosted=false;
                for (int attempt=0;attempt<32 && !hosted;++attempt) {
                    NetHost host{}; host.m_Port=std::uniform_int_distribution<unsigned>(28000,60000)(random); host.m_dwMaxPlayers=1;
                    std::snprintf(host.m_sName,sizeof host.m_sName,"%s","Local");
                    hosted=server->HostGame(host);
                }
                if (!hosted) throw std::runtime_error("no loopback port for the local server");
                if (!server->LoadBinaries()) {
                    ServerInterfaceCall access;
                    throw std::runtime_error(access.available() ? access.manager()->m_ClassMgr.error() : "local server retired while loading the game");
                }
                char initialized[]={char(SERVERSHELL_INIT),'\n'};
                if (server->SendToServerShell(initialized,sizeof initialized)!=LT_OK) throw std::runtime_error("game server initialization failed");
                char host[128]{}; unsigned short port=0;
                if (!server->GetTcpIpAddress(host,sizeof host,port)) throw std::runtime_error("local server address unavailable");
                CBaseDriver* driver=nullptr;
                {
                    ServerInterfaceCall access;
                    if (access.available()) driver=access.manager()->m_NetMgr.FindDriver(const_cast<char*>("local"));
                }
                if (!driver) throw std::runtime_error("local server has no in-process driver");
                started.set_value({"127.0.0.1:"+std::to_string(port),driver}); announced=true;
                platform::FrameLimiter limiter(60);
                while (!state->stop) {
                    if (!server->Update(0)) {
                        char error[1024]{}; server->GetErrorString(error,sizeof error);
                        throw std::runtime_error(std::string("local server update failed: ")+error);
                    }
                    limiter.wait();
                }
            } catch (...) {
                if (!announced) started.set_exception(std::current_exception());
                else { std::lock_guard lock(state->mutex); state->failure=std::current_exception(); }
            }
            if (server) DeleteServer();
            configure_engine_resources(nullptr);
            running=false;
        });
    } catch (...) { running=false; throw; }
    try { std::tie(address_,driver_)=address.get(); }
    catch (...) { state_->stop=true; if (state_->thread.joinable()) state_->thread.join(); throw; }
}
LocalServer::~LocalServer() { state_->stop=true; if (state_->thread.joinable()) state_->thread.join(); }
void LocalServer::update() {
    std::lock_guard lock(state_->mutex);
    if (state_->failure) std::rethrow_exception(std::exchange(state_->failure,nullptr));
}
}
