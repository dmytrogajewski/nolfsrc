#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace nolf::platform {
// Application preferences, separate from game console state and game assets.
// Loading is read-only. save() stages a bounded document and replaces atomically.
class Settings {
public:
    Settings();
    explicit Settings(std::filesystem::path path);
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    void set(std::string key, std::string value);
    void save() const;
private:
    std::filesystem::path path_;
    std::map<std::string, std::string, std::less<>> values_;
};
}
