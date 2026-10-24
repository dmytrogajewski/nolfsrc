#define __SERVERAPI_EXPORT__
#include "engine/server/servermgr.h"
#include "engine/server/server_access.h"
#include "engine/portable/engine_resources.h"
#include "game/Shared/NetDefs.h"
#include "platform/termination.h"
#include "platform/timing.h"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <stdexcept>

namespace {
struct Options {
    std::filesystem::path data, module, config, log;
    std::vector<std::string> worlds;
    std::string name = "NOLF Linux", bind;
    unsigned port = 27888, players = 16, ticks = 0;
    GameType mode = DEATHMATCH;
};
unsigned number(std::string_view text, unsigned maximum, std::string_view option) {
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || !value || value > maximum)
        throw std::invalid_argument(std::string(option)+" expects 1.."+std::to_string(maximum));
    return value;
}
void help() {
    std::cout << "Usage: nolf-server --data DIRECTORY --module FILE --world ASSET [options]\n"
        "  --world ASSET       Level asset name; repeat for a rotation of up to 50 levels\n"
        "  --mode dm|assault   Game mode (default: dm)\n"
        "  --name NAME         Session name, at most 24 bytes (default: NOLF Linux)\n"
        "  --port PORT         UDP port (default: 27888)\n"
        "  --bind IPV4         Local IPv4 address (default: all interfaces)\n"
        "  --players COUNT     Maximum players, 1..128 (default: 16)\n"
        "  --config FILE       Server console configuration\n"
        "  --log FILE          Append host and game logs to FILE\n"
        "  --ticks COUNT       Stop after COUNT host updates (60 per second)\n"
        "  --help              Show this help\n"
        "LAN hosting; master-server listing is disabled. Stop with Ctrl+C or SIGTERM.\n";
}
Options parse(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (index+1 == argc) throw std::invalid_argument("missing value for "+std::string(option));
        const std::string_view value(argv[++index]);
        if (value.empty()) throw std::invalid_argument("empty value for "+std::string(option));
        if (option == "--data") options.data = value;
        else if (option == "--module") options.module = value;
        else if (option == "--config") options.config = value;
        else if (option == "--log") options.log = value;
        else if (option == "--world") {
            if (options.worlds.size() == MAX_GAME_LEVELS) throw std::invalid_argument("at most 50 levels are supported");
            auto world = nolf::platform::asset_key(value);
            if (world.ends_with(".dat")) world.resize(world.size()-4);
            if (world.empty() || world.size() >= NML_LEVEL) throw std::invalid_argument("invalid level asset name");
            options.worlds.push_back(std::move(world));
        } else if (option == "--mode") {
            if (value == "dm") options.mode = DEATHMATCH;
            else if (value == "assault") options.mode = COOPERATIVE_ASSAULT;
            else throw std::invalid_argument("--mode expects dm or assault");
        } else if (option == "--name") {
            if (value.size() >= MAX_SESSION_NAME || std::any_of(value.begin(),value.end(),[](unsigned char c) { return c < 32 || c == 127; }))
                throw std::invalid_argument("--name expects 1..24 bytes without control characters");
            options.name = value;
        } else if (option == "--bind") {
            // A numeric address is also safe to pass as one console token.
            if (value.find_first_not_of("0123456789.") != value.npos)
                throw std::invalid_argument("--bind expects a numeric IPv4 address");
            nolf::platform::ipv4_bytes(std::string(value));
            options.bind = value;
        } else if (option == "--port") options.port = number(value,65535,option);
        else if (option == "--players") options.players = number(value,MAX_MULTI_PLAYERS,option);
        else if (option == "--ticks") options.ticks = number(value,60u*60*24*7,option);
        else throw std::invalid_argument("unknown option "+std::string(option));
    }
    if (options.data.empty() || options.module.empty() || options.worlds.empty())
        throw std::invalid_argument("--data, --module and at least one --world are required");
    options.data = std::filesystem::canonical(options.data);
    options.module = std::filesystem::canonical(options.module);
    return options;
}
void command(ServerInterface& server, std::string text) {
    if (server.RunConsoleString(text.data()) != LT_OK) throw std::runtime_error("server command failed: "+text);
}
class ServerOwner {
public:
    ServerInterface* interface = nullptr;
    ~ServerOwner() { if (interface) DeleteServer(); }
};
struct ResourceOwner {
    ~ResourceOwner() { nolf::configure_engine_resources(nullptr); }
};
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { help(); return 0; }
        auto options = parse(argc,argv);
        nolf::platform::TerminationRequest termination;
        if (!options.log.empty()) nolf::platform::set_log_file(options.log);
        auto resources = std::make_shared<nolf::Resources>(options.data);
        nolf::configure_engine_resources(resources);
        ResourceOwner resource_owner;
        ServerOwner owner;
        auto guid = kNolfApplicationGuid;
        const auto status = CreateServer(SI_VERSION,guid,&owner.interface);
        if (status != SI_OK) throw std::runtime_error("server creation failed: "+std::to_string(status));
        auto& server = *owner.interface;
        {
            ServerInterfaceCall access;
            if (!access.available()) throw std::runtime_error("server retired during startup");
            auto& manager = *access.manager();
            manager.m_FileMgr.mount_game(options.data);
            for (const auto& world : options.worlds)
                if (!manager.m_FileMgr.locate(world+".dat").file) throw std::runtime_error("level asset not found: "+world);
            manager.m_ClassMgr.configure(options.module,{resources.get(),"cres",1033});
        }
        if (!options.config.empty()) {
            auto filename = options.config.string();
            if (server.LoadConfigFile(filename.data()) != LT_OK) throw std::runtime_error("failed to load server configuration");
        }
        command(server,"MasterServer none");
        command(server,"NetMaxPlayers "+std::to_string(options.players));
        if (!options.bind.empty()) command(server,"BindIP "+options.bind);
        NetGame game{};
        std::copy(options.name.begin(),options.name.end(),game.m_sSession);
        game.m_byType = options.mode;
        game.m_byNumLevels = static_cast<uint8>(options.worlds.size());
        for (std::size_t index = 0; index < options.worlds.size(); ++index)
            std::copy(options.worlds[index].begin(),options.worlds[index].end(),game.m_sLevels[index]);
        if (!server.SetGameInfo(&game,sizeof(game))) throw std::runtime_error("failed to set game information");
        if (!server.InitNetworking(nullptr,0)) throw std::runtime_error("network initialization failed");
        NetService* services = nullptr;
        if (!server.GetServiceList(services)) throw std::runtime_error("network service enumeration failed");
        HNETSERVICE internet = nullptr;
        for (auto* service = services; service; service = service->m_pNext)
            if (service->m_dwFlags & NETSERVICE_TCPIP) { internet = service->m_handle; break; }
        server.FreeServiceList(services);
        if (!internet || !server.SelectService(internet)) throw std::runtime_error("IPv4 UDP service unavailable");
        NetHost host{};
        host.m_Port = options.port; host.m_dwMaxPlayers = options.players;
        std::copy(options.name.begin(),options.name.end(),host.m_sName);
        if (!server.HostGame(host)) throw std::runtime_error("cannot host UDP port "+std::to_string(options.port));
        if (!server.LoadBinaries()) {
            ServerInterfaceCall access;
            throw std::runtime_error(access.available() ? access.manager()->m_ClassMgr.error() : "server retired while loading game");
        }
        char initialized[] = {char(SERVERSHELL_INIT),'\n'};
        if (server.SendToServerShell(initialized,sizeof(initialized)) != LT_OK)
            throw std::runtime_error("game shell initialization failed");
        {
            ServerInterfaceCall access;
            if (!access.available() || access.manager()->DoStartWorld(options.worlds.front().data(),3,dsi_GetTime()) != LT_OK)
                throw std::runtime_error("game level startup failed");
        }
        char address[128]{};
        unsigned short port = 0;
        if (!server.GetTcpIpAddress(address,sizeof(address),port)) throw std::runtime_error("host address unavailable");
        std::cout << "Hosting \"" << options.name << "\" on " << address << ':' << port
            << " (" << options.players << " players); world " << options.worlds.front() << std::endl;
        nolf::platform::FrameLimiter limiter(60);
        unsigned updates = 0;
        while (!termination.requested() && (!options.ticks || updates < options.ticks)) {
            if (!server.Update(0)) {
                char error[1024]{}; server.GetErrorString(error,sizeof(error));
                throw std::runtime_error(std::string("server update failed: ")+error);
            }
            ++updates;
            limiter.wait();
        }
        std::cout << "Stopping server after " << updates << " host updates" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "nolf-server: " << error.what() << '\n'; return 1;
    }
}
