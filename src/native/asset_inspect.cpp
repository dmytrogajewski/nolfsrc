#include "engine/portable/assets.h"
#include "engine/portable/resources.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cout << "Usage: nolf-asset-inspect <archive.rez> [more archives...]\n"
                         "       nolf-asset-inspect --data <imported-data-directory>\n";
            return 0;
        }
        nolf::Assets assets;
        if (argc == 3 && std::string_view(argv[1]) == "--data") {
            assets.mount_game(argv[2]);
            nolf::Resources resources(argv[2]);
            std::cout << "Game: " << resources.string("cres", 1) << '\n';
        } else {
            for (int i = 1; i < argc; ++i) assets.mount_archive(argv[i]);
        }
        std::size_t worlds = 0;
        for (const auto& name : assets.names()) if (name.ends_with(".dat")) {
            std::cout << name << '\n'; ++worlds;
        }
        std::cout << assets.names().size() << " assets, " << worlds << " worlds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
