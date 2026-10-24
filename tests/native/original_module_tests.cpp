#include "engine/portable/game_module.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/server/world_state.h"
#include "engine/server/object_properties.h"
#include <iostream>
#include <stdexcept>
#include <map>
#include <fstream>
#include <sstream>
#include <set>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) throw std::runtime_error("Expected native game module, optional data directory and original Windows class reference");
        nolf::GameModule module(argv[1], nullptr);
        for (const char* name : {"BaseClass", "CPlayerObj", "GameStartPoint", "WorldProperties", "CinematicTrigger", "Prop"})
            if (!module.find(name)) throw std::runtime_error(std::string("Missing original game class: ") + name);
        if (module.classes().size() < 100) throw std::runtime_error("Incomplete original game class table");
        std::cout << "Original NOLF module: " << module.classes().size() << " native class definitions validated\n";
        std::set<std::string> reference_names;
        if (argc == 4) {
            std::ifstream reference(argv[3]);
            std::string line;
            if (!std::getline(reference, line) || line != "NOLF CLASS REFERENCE 1")
                throw std::runtime_error("Invalid original Windows class reference");
            while (std::getline(reference, line)) {
                std::istringstream fields(line);
                std::string name, parent, extra;
                unsigned flags;
                int properties;
                if (!(fields >> name >> parent >> flags >> properties) || fields >> extra || !reference_names.insert(name).second)
                    throw std::runtime_error("Invalid or duplicate reference class record");
                const auto* definition = module.find(name);
                if (!definition || parent != (definition->m_ParentClass ? definition->m_ParentClass->m_ClassName : "-") ||
                    flags != definition->m_ClassFlags || properties != definition->m_nProps)
                    throw std::runtime_error("Native class differs from original Windows metadata: " + name);
            }
            if (!reference.eof() || reference_names.size() < 100)
                throw std::runtime_error("Incomplete original Windows class reference");
            std::cout << "Original Windows class parity: " << reference_names.size() << " names, parents, flags and property counts\n";
        }
        if (argc >= 3) {
            nolf::Assets assets; assets.mount_game(argv[2]);
            CServerWorld world;
            std::size_t worlds = 0, objects = 0, properties = 0;
            std::map<std::string, std::size_t> missing;
            for (const auto& name : assets.names()) {
                if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
                nolf::AssetStream stream(assets.locate(name));
                WorldLoadRequest request; request.m_pStream = &stream;
                if (world.Load(&request) != 0 || world.LoadObjects(&stream) != LT_OK)
                    throw std::runtime_error(name + ": " + world.LastLoadError());
                for (const auto& object : world.ObjectDefinitions()) {
                    ObjectProperties decoded(std::span<const WorldProperty>(object.properties));
                    properties += object.properties.size();
                    if (!module.find(object.class_name)) ++missing[object.class_name];
                    ++objects;
                }
                ++worlds;
            }
            if (worlds < 100) throw std::runtime_error("Original world corpus is incomplete");
            std::cout << "Original world class coverage: " << worlds << " levels, " << objects << " object definitions, " << properties << " typed properties\n";
            for (const auto& [name, count] : missing) {
                if (reference_names.empty() || reference_names.contains(name))
                    throw std::runtime_error("Missing native implementation of original class: " + name);
                std::cout << "Legacy unregistered class absent from both modules: " << name << " (" << count << " objects)\n";
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
