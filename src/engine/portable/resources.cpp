#include "engine/portable/resources.h"
#include <algorithm>
#include <stdexcept>
namespace nolf {
Resources::Resources(const std::filesystem::path& data) : files_(data / "resources") {}
std::vector<std::byte> Resources::get(std::string_view module, const std::string& suffix, std::uint32_t language) const {
    auto base = platform::asset_key(module);
    if (base.find('/') != base.npos) throw std::invalid_argument("resource module must be a single name");
    for (auto id : {language, 1033u, 0u}) {
        const auto path = files_.resolve(base + '/' + std::to_string(id) + '/' + suffix);
        if (!path.empty()) return platform::read_file(path);
    }
    throw std::runtime_error("missing resource: " + base + '/' + suffix);
}
std::string Resources::string(std::string_view module, std::uint32_t id, std::uint32_t language) const {
    auto bytes = get(module, "strings/" + std::to_string(id) + ".txt", language);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::vector<std::byte> Resources::raw(std::string_view module, std::string_view type, std::string_view id, std::uint32_t language) const {
    return get(module, "raw/" + platform::asset_key(type) + '/' + platform::asset_key(id) + ".bin", language);
}
}
