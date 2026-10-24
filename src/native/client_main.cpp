#include "engine/client/native_client.h"
#include "modern/render/rdev.h"
#include "platform/logging.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include "platform/timing.h"
#include <charconv>
#include <map>
#include <sstream>
#include <iostream>
#include <stdexcept>

namespace {
struct Options {
    std::filesystem::path data, module, server_module, log;
    std::map<unsigned,std::filesystem::path> captures;
    std::multimap<unsigned,nolf::platform::InputEvent> input;
    std::multimap<unsigned,std::string> commands;
    std::vector<std::string> console;
    int width = 1280, height = 720;
    unsigned frames = 0, max_fps = 0;
    bool fullscreen = false, vsync = true;
};
unsigned number(std::string_view text, unsigned maximum, std::string_view option) {
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || !value || value > maximum)
        throw std::invalid_argument(std::string(option)+" expects 1.."+std::to_string(maximum));
    return value;
}
void help() {
    std::cout << "Usage: nolf-client --data DIRECTORY [options] [+variable value ...]\n"
        "  --data DIRECTORY    Imported game data (see docs/NATIVE.md)\n"
        "  --module FILE       Client game module (default: libnolf_cshell beside this program)\n"
        "  --server-module FILE  Server game module for local games (default: libnolf_object)\n"
        "  --width PIXELS      Window width (default: 1280)\n"
        "  --height PIXELS     Window height (default: 720)\n"
        "  --fullscreen        Start fullscreen\n"
        "  --no-vsync          Present without waiting for the display\n"
        "  --log FILE          Append engine and game logs to FILE\n"
        "  --frames COUNT      Quit after COUNT frames\n"
        "  --max-fps RATE      Frame rate cap (default: 240 without vsync)\n"
        "  --capture N FILE    Write frame N as a BMP (repeatable)\n"
        "  --help              Show this help\n"
        "Console variables follow LithTech's command line, e.g. +runworld worlds/single/m01s01.\n";
}
Options parse(int argc, char** argv, const std::filesystem::path& program) {
    Options options;
    options.module = program.parent_path()/(std::string(NOLF_MODULE_PREFIX)+"nolf_cshell"+NOLF_MODULE_SUFFIX);
    options.server_module = program.parent_path()/(std::string(NOLF_MODULE_PREFIX)+"nolf_object"+NOLF_MODULE_SUFFIX);
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help") { help(); std::exit(0); }
        if (option == "--fullscreen") { options.fullscreen = true; continue; }
        if (option == "--no-vsync") { options.vsync = false; continue; }
        if (option.starts_with("+")) {
            std::string command(option);
            if (index+1 < argc && argv[index+1][0] != '+' && argv[index+1][0] != '-') command += " \""+std::string(argv[++index])+"\"";
            options.console.push_back(std::move(command)); continue;
        }
        if (index+1 == argc) throw std::invalid_argument("missing value for "+std::string(option));
        const std::string_view value(argv[++index]);
        if (option == "--data") options.data = value;
        else if (option == "--module") options.module = value;
        else if (option == "--server-module") options.server_module = value;
        else if (option == "--log") options.log = value;
        else if (option == "--width") options.width = int(number(value,16384,option));
        else if (option == "--height") options.height = int(number(value,16384,option));
        else if (option == "--frames") options.frames = number(value,100000000,option);
        else if (option == "--max-fps") options.max_fps = number(value,1000,option);
        else if (option == "--input") {
            // Scripted input for automated runs: "FRAME key CODE down|up" with
            // engine (DirectInput) key codes, "FRAME mouse DX DY" or
            // "FRAME button N down|up" (1 left, 2 middle, 3 right).
            const auto bytes = nolf::platform::read_file(std::filesystem::path(value));
            std::istringstream script(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
            std::string line;
            while (std::getline(script,line)) {
                std::istringstream words(line); unsigned frame = 0; std::string kind;
                if (!(words >> frame >> kind) || kind.starts_with("#")) continue;
                nolf::platform::InputEvent event{};
                if (kind == "key") {
                    std::string state; event.type = nolf::platform::InputType::key;
                    if (!(words >> std::hex >> event.code >> state)) throw std::invalid_argument("bad input line: "+line);
                    event.pressed = state == "down";
                } else if (kind == "button") {
                    std::string state; event.type = nolf::platform::InputType::mouse_button;
                    if (!(words >> event.code >> state)) throw std::invalid_argument("bad input line: "+line);
                    event.pressed = state == "down";
                } else if (kind == "mouse") {
                    event.type = nolf::platform::InputType::mouse_motion;
                    if (!(words >> event.x >> event.y)) throw std::invalid_argument("bad input line: "+line);
                } else if (kind == "console") {
                    std::string text; std::getline(words,text);
                    options.commands.emplace(frame,text.substr(text.find_first_not_of(' ')));
                    continue;
                } else throw std::invalid_argument("bad input line: "+line);
                options.input.emplace(frame,event);
            }
        }
        else if (option == "--capture") {
            const auto frame = number(value,100000000,option);
            if (index+1 == argc) throw std::invalid_argument("--capture needs a file");
            options.captures[frame] = argv[++index];
        }
        else throw std::invalid_argument("unknown option "+std::string(option));
    }
    if (options.data.empty()) throw std::invalid_argument("--data is required");
    return options;
}
// The device follows the desktop; both outlive the engine.
struct Device {
    explicit Device(nolf::platform::Desktop& desktop, int width, int height, bool windowed, bool vsync) {
        if (!rd_CreateDevice(&desktop,std::uint32_t(width),std::uint32_t(height),windowed,vsync ? 1 : 0,1))
            throw std::runtime_error("cannot create the OpenGL render device");
        nolf::platform::log("client",std::string("OpenGL ")+rd_Describe());
    }
    ~Device() { rd_DestroyDevice(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
};
}
int main(int argc, char** argv) {
    try {
        const auto options = parse(argc,argv,std::filesystem::absolute(argv[0]));
        if (!options.log.empty()) nolf::platform::set_log_file(options.log);
        nolf::platform::DesktopConfig config;
        config.width = options.width; config.height = options.height; config.fullscreen = options.fullscreen; config.vsync = options.vsync;
        nolf::platform::Desktop desktop(config);
        int width = 0, height = 0; desktop.drawable_size(width,height);
        Device device(desktop,width,height,!options.fullscreen,options.vsync);
        auto assets = std::make_shared<nolf::Assets>(); assets->mount_game(options.data);
        nolf::NativeClientServices services{assets,std::make_shared<nolf::Resources>(options.data),
            nolf::platform::make_freetype_fonts(nolf::platform::system_font_locator()),
            nolf::platform::make_ffmpeg_audio,nolf::platform::make_sdl_audio,nolf::platform::make_ffmpeg_video,
            nolf::platform::make_dmusic_performance,{},options.data,options.server_module};
        auto client = nolf::NativeClient::create(desktop,std::move(services));
        nolf::ClientGameModule module(options.module);
        const auto shell = client->create_shell(module);
        client->load_config();
        for (const auto& command : options.console) client->console_command(command);
        if (client->initialize(shell) != LT_OK) throw std::runtime_error("the game failed to initialize");
        auto previous = nolf::platform::monotonic_nanoseconds();
        const auto cap = options.max_fps ? options.max_fps : options.vsync ? 0u : 240u;
        std::optional<nolf::platform::FrameLimiter> limiter;
        if (cap) limiter.emplace(cap);
        for (unsigned frame = 1;; ++frame) {
            if (const auto capture = options.captures.find(frame); capture != options.captures.end()) client->capture(capture->second);
            for (auto [command,end] = options.commands.equal_range(frame); command != end; ++command) client->console_command(command->second);
            auto events = desktop.poll();
            for (auto [scripted,end] = options.input.equal_range(frame); scripted != end; ++scripted) events.push_back(scripted->second);
            const auto now = nolf::platform::monotonic_nanoseconds();
            const auto elapsed = float(double(now-previous)/1e9); previous = now;
            if (!client->frame(shell,events,std::min(elapsed,0.25f))) break;
            if (options.frames && frame >= options.frames) break;
            if (limiter) limiter->wait();
        }
        client->terminate(shell);
        client->save_config();
        if (!client->shutdown_message().empty()) std::cerr << client->shutdown_message() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "nolf-client: " << error.what() << '\n';
        return 1;
    }
}
