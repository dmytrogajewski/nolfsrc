#include "platform/module.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>

namespace nolf::platform {
std::filesystem::path module_filename(std::string_view stem) { return std::string(stem) + ".dll"; }
struct Module::Impl {
    HMODULE handle = nullptr;
    ~Impl() { if (handle) FreeLibrary(handle); }
};
Module::Module(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    HMODULE handle = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle) throw std::runtime_error("load module failed: " + std::to_string(GetLastError()));
    impl_->handle = handle;
}
Module::~Module() = default;
Module::Module(Module&&) noexcept = default;
Module& Module::operator=(Module&&) noexcept = default;
void* Module::symbol(const char* name) const {
    auto fn = GetProcAddress(impl_->handle, name);
    if (!fn) throw std::runtime_error("missing module symbol: " + std::string(name));
    return reinterpret_cast<void*>(fn);
}
}
