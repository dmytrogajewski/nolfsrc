#include "platform/user_files.h"
#include "platform/system.h"
#include <stdexcept>

namespace fs=std::filesystem;
namespace nolf::platform {
UserFiles::UserFiles() : UserFiles(user_directories().data) {}
UserFiles::UserFiles(fs::path root) : root_(fs::absolute(std::move(root)).lexically_normal()) {}
fs::path UserFiles::path(std::string_view logical) const {
    const auto key=asset_key(logical);
    auto current=root_;
    std::size_t start=0;
    while (start < key.size()) {
        const auto end=key.find('/',start);
        const auto component=key.substr(start,end == key.npos ? key.size()-start : end-start);
        fs::path match;
        if (fs::is_directory(current)) {
            for (const auto& entry : fs::directory_iterator(current)) {
                if (asset_key(entry.path().filename().string()) != component) continue;
                if (!match.empty()) throw std::runtime_error("ambiguous writable game path: "+key);
                if (entry.is_symlink()) throw std::runtime_error("writable game path contains a symbolic link: "+key);
                match=entry.path();
            }
        }
        current=match.empty() ? current/component : match;
        if (end == key.npos) break;
        start=end+1;
    }
    return current;
}
bool UserFiles::file_exists(std::string_view logical) const { return fs::is_regular_file(path(logical)); }
bool UserFiles::directory_exists(std::string_view logical) const { return fs::is_directory(path(logical)); }
void UserFiles::create_directory(std::string_view logical) const { fs::create_directories(path(logical)); }
std::vector<std::byte> UserFiles::read(std::string_view logical,std::size_t maximum) const {
    InputFile input(path(logical));
    if (input.size() > maximum) throw std::length_error("writable game file is too large");
    std::vector<std::byte> bytes(static_cast<std::size_t>(input.size())); input.read_at(0,bytes); return bytes;
}
void UserFiles::write(std::string_view logical,std::span<const std::byte> bytes) const { atomic_write(path(logical),bytes); }
}
