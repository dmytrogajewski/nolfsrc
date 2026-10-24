#pragma once
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>

// Points the engine's user directories at an isolated temporary root.
class ScopedUserHome {
public:
    ScopedUserHome() : root_(std::filesystem::temp_directory_path()/("nolf-user-"+std::to_string(std::random_device{}()))) {
        std::filesystem::create_directories(root_);
        if (const char* value = std::getenv("NOLF_USER_HOME")) previous_ = value;
        set(root_.string().c_str());
    }
    ~ScopedUserHome() { set(previous_ ? previous_->c_str() : nullptr); std::error_code ignored; std::filesystem::remove_all(root_,ignored); }
    ScopedUserHome(const ScopedUserHome&) = delete;
    ScopedUserHome& operator=(const ScopedUserHome&) = delete;
    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path data() const { return root_/"data"; }
private:
    static void set(const char* value) {
#ifdef _WIN32
        _putenv_s("NOLF_USER_HOME",value ? value : "");
#else
        if (value) setenv("NOLF_USER_HOME",value,1); else unsetenv("NOLF_USER_HOME");
#endif
    }
    std::filesystem::path root_;
    std::optional<std::string> previous_;
};
