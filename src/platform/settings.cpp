#include "platform/settings.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace nolf::platform {
namespace {
constexpr std::size_t max_document = 1024 * 1024;
void validate(std::string_view key, std::string_view value) {
    if (key.empty() || key.size() > 256 || value.size() > 65535 ||
        key.find('\0') != key.npos || value.find('\0') != value.npos)
        throw std::runtime_error("Invalid application preference");
}
}
Settings::Settings() : Settings(user_directories().config / "settings.cfg") {}
Settings::Settings(std::filesystem::path path) : path_(std::move(path)) {
    if (!std::filesystem::exists(path_)) return;
    InputFile file(path_);
    if (file.size() > max_document) throw std::runtime_error("Preferences file is too large");
    std::string text(static_cast<std::size_t>(file.size()), '\0');
    file.read_at(0, std::as_writable_bytes(std::span(text.data(), text.size())));
    std::istringstream input(text);
    std::string header;
    if (!std::getline(input, header)) throw std::runtime_error("Missing preferences header");
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header != "NOLF SETTINGS 1") throw std::runtime_error("Invalid preferences header");
    for (;;) {
        input >> std::ws;
        if (input.eof()) break;
        std::string key, value;
        if (!(input >> std::quoted(key) >> std::quoted(value))) throw std::runtime_error("Invalid preferences entry");
        validate(key, value);
        if (!values_.emplace(std::move(key), std::move(value)).second) throw std::runtime_error("Duplicate preferences entry");
    }
}
std::string Settings::get(std::string_view key, std::string_view fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? std::string(fallback) : found->second;
}
void Settings::set(std::string key, std::string value) {
    validate(key, value);
    values_.insert_or_assign(std::move(key), std::move(value));
}
void Settings::save() const {
    std::ostringstream output;
    output << "NOLF SETTINGS 1\n";
    for (const auto& [key, value] : values_) {
        output << std::quoted(key) << ' ' << std::quoted(value) << '\n';
        if (output.tellp() > static_cast<std::streamoff>(max_document)) throw std::runtime_error("Preferences file is too large");
    }
    const auto text = output.str();
    atomic_write(path_, std::as_bytes(std::span(text.data(), text.size())));
}
}
