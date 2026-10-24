#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace nolf::platform {
inline constexpr std::uint32_t module_api_version = 1;
struct ModuleHeader { std::uint32_t version, size; };
using ModuleSetup = const ModuleHeader* (*)(std::uint32_t host_version);
std::filesystem::path module_filename(std::string_view stem);

class Module {
public:
    explicit Module(const std::filesystem::path& path);
    ~Module();
    Module(Module&&) noexcept;
    Module& operator=(Module&&) noexcept;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    void* symbol(const char* name) const;
    const ModuleHeader& setup(std::uint32_t minimum_size = sizeof(ModuleHeader)) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
