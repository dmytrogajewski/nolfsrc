#include "platform/module.h"
#include <dlfcn.h>
#include <stdexcept>
#include <string>

namespace nolf::platform {
std::filesystem::path module_filename(std::string_view stem) { return std::string(stem) + ".so"; }
struct Module::Impl {
    void* handle = nullptr;
    ~Impl() { if (handle) dlclose(handle); }
};
Module::Module(const std::filesystem::path& path) : impl_(std::make_unique<Impl>()) {
    void* handle = dlopen(std::filesystem::absolute(path).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) throw std::runtime_error(std::string("load module: ") + dlerror());
    impl_->handle = handle;
}
Module::~Module() = default;
Module::Module(Module&&) noexcept = default;
Module& Module::operator=(Module&&) noexcept = default;
void* Module::symbol(const char* name) const {
    dlerror();
    void* result = dlsym(impl_->handle, name);
    if (const char* error = dlerror()) throw std::runtime_error(std::string("module symbol: ") + error);
    return result;
}
}
