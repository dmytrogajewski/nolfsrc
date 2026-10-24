#include "engine/portable/engine_resources.h"
#include "engine/server/engine_config.h"
#include "platform/system.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>

extern int g_JoinPort;
extern int g_ShowNetInfo;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string formatted(int code, ...) {
    std::array<char, 1024> output{};
    va_list arguments;
    va_start(arguments, code);
    dsi_SetupMessage(output.data(), output.size(), code, arguments);
    va_end(arguments);
    return output.data();
}
void write(const std::filesystem::path& file, std::string_view value) {
    nolf::platform::atomic_write(file, std::as_bytes(std::span(value)));
}
}
int main(int argc, char** argv) {
    try {
        require(!nolf::engine_resources_ready(), "startup requires a configured resource catalogue");
        const auto root = std::filesystem::temp_directory_path() / ("nolf-services-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
        struct Cleanup { std::filesystem::path path; ~Cleanup() { nolf::configure_engine_resources(nullptr); std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{root};
        auto strings = root / "resources/ltmsg/1033/strings";
        std::filesystem::create_directories(strings);
        write(strings / "1.txt", "Engine error %1!d!.");
        write(strings / "2.txt", "Missing %1!s!.");
        write(strings / "3.txt", "No arguments.");
        auto resources = std::make_shared<nolf::Resources>(root);
        nolf::configure_engine_resources(resources, 1049);
        require(nolf::engine_resources_ready() && formatted(1, 42) == "Engine error 42." &&
                formatted(2, "sample.dat") == "Missing sample.dat.", "native engine messages use imported resources and language fallback");
        std::array<char, 5> small{'x', 'x', 'x', 'x', 'x'};
        require(dsi_SetupMessage(small.data(), small.size(), 3, nullptr) == 4 && std::string(small.data()) == "No a",
                "engine messages accept empty argument lists and bound destination writes");
        require(formatted(999) == "Engine error 999", "missing engine resources retain their error code");
        std::atomic<bool> good{true};
        std::jthread reader([&] {
            for (int i = 0; i < 200; ++i) {
                const auto result = formatted(1, 42);
                if (result != "Engine error 42." && result != "Engine error 1") good = false;
            }
        });
        for (int i = 0; i < 200; ++i) { nolf::configure_engine_resources(nullptr); nolf::configure_engine_resources(resources); }
        reader.join();
        require(good, "resource publication retains catalogue ownership during formatting");
        auto* bindings = dsi_GetConsoleBindings();
        require(bindings && dsi_GetNumConsoleBindings() == 93, "native engine console bindings");
        bool foundPort = false;
        for (int i = 0; i < dsi_GetNumConsoleBindings(); ++i) {
            if (std::strcmp(bindings[i].m_pName, "IPClientPort")) continue;
            const auto saved = g_ShowNetInfo;
            *bindings[i].m_pInt = 65535;
            require(g_JoinPort == 65535 && g_ShowNetInfo == saved, "console port storage has the binding's full integer width");
            foundPort = true;
        }
        require(foundPort, "client port binding exists");
        if (argc == 3 && std::string_view(argv[1]) == "--data") {
            nolf::configure_engine_resources(std::make_shared<nolf::Resources>(argv[2]));
            require(formatted(1, 42) == "Internal DirectEngine error: 42.", "original engine resource formatting");
        } else require(argc == 1, "usage: nolf-engine-services-tests [--data game-data]");
        std::cout << "native engine resource ownership, formatting, and configuration passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
