#include "engine/portable/client_module.h"
#include <iostream>
#include <optional>
#include <stdexcept>
static void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } require(rejected,"incompatible client module accepted"); }
int main(int argc,char** argv) {
    try {
        require(argc==4,"missing native module fixture paths");
        std::optional<nolf::ClientGameModule> module; module.emplace(argv[1]);
        auto lease=module->lease(); const std::weak_ptr<const void> weak=lease;
        module.reset(); require(!weak.expired(),"client module lease did not retain the loaded code");
        lease.reset(); require(weak.expired(),"client module remained retained after its last owner");
        rejects([&] { nolf::ClientGameModule invalid(argv[2]); }); // server ABI
        rejects([&] { nolf::ClientGameModule invalid(argv[3]); }); // short descriptor
        rejects([&] { nolf::ClientGameModule missing(std::filesystem::path(argv[1]).parent_path()/"missing-client-module"); });
        nolf::ClientGameModule reload(argv[1]);
        std::cout << "Original native client descriptor/load/unload checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
