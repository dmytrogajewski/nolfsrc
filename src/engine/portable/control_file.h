#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nolf {
// LithTech control files (music level definitions): "[section]" headers,
// ';' comments, '#ifdef/#ifndef/#else/#endif' against caller defines, and
// keys whose words are split by " =,\t\r". Names compare without case.
class ControlFile {
public:
    struct Key {
        std::string name;
        std::vector<std::string> words;
        std::optional<int> integer(std::size_t word) const;
        std::optional<double> number(std::size_t word) const;
    };
    static ControlFile parse(std::span<const std::byte>,std::span<const std::string> defines = {});
    // Every key with this name, in file order, from the unnamed first section
    // when no section is given.
    std::vector<const Key*> keys(std::string_view name,std::string_view section = {}) const;
    const Key* key(std::string_view name,std::string_view section = {}) const;
private:
    struct Section { std::string name; std::vector<Key> keys; };
    std::vector<Section> sections_;
};
}
