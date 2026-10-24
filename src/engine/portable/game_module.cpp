#include "game_module.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nolf {
namespace {
std::string key(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.find('\0') != name.npos) throw std::runtime_error("invalid game class name");
    return std::string(name);
}
std::size_t alignment(const ClassDef& definition) {
    return std::max<std::size_t>(definition.m_ClassObjectAlignment, alignof(std::max_align_t));
}
}
struct GameModule::Object::Owner {
    explicit Owner(const std::filesystem::path& path, ILTServer* interface) : module(path), server(interface) {
        const auto& header = module.setup(sizeof(ServerModuleApi));
        api = reinterpret_cast<const ServerModuleApi*>(&header);
        if (api->magic != server_module_magic || api->cpp_abi != NOLF_CPP_ABI_ID || api->pointer_size != sizeof(void*) ||
            api->class_size != sizeof(ClassDef) || api->property_size != sizeof(PropDef) || api->base_size != sizeof(BaseClass) ||
            api->object_version != SERVEROBJ_VERSION || api->shell_version != SERVERSHELL_VERSION ||
            api->sdk_abi != sdk_abi_fingerprint ||
            !api->objects || !api->create_shell || !api->delete_shell || !api->bind_log)
            throw std::runtime_error("incompatible native server module descriptor");
        api->bind_log([](const char* subsystem, std::size_t subsystem_size, const char* message, std::size_t message_size) {
            platform::log(std::string_view(subsystem, subsystem_size), std::string_view(message, message_size));
        });
        int count = 0, version = 0;
        auto** table = api->objects(&count, server, &version);
        if (version != SERVEROBJ_VERSION || count <= 0 || count > 65535 || !table)
            throw std::runtime_error("invalid native game class table");
        definitions.assign(table, table + count);
        std::unordered_set<ClassDef*> members;
        for (auto* definition : definitions) {
            if (!definition || !members.insert(definition).second || !definition->m_ClassName)
                throw std::runtime_error("invalid or duplicate native game class");
            if (!names.emplace(key(definition->m_ClassName), definition).second)
                throw std::runtime_error("ambiguous native game class name");
            const auto size = definition->m_ClassObjectSize;
            const auto align = definition->m_ClassObjectAlignment;
            if (size < sizeof(BaseClass) || size > 64 * 1024 * 1024 || !align || (align & (align - 1)) || align > 4096 || size % align)
                throw std::runtime_error("invalid native game object size or alignment");
            if (definition->m_nProps < 0 || (definition->m_nProps && !definition->m_Props))
                throw std::runtime_error("invalid native class property table");
            for (int i = 0; i < definition->m_nProps; ++i) {
                const auto& prop = definition->m_Props[i];
                if (!prop.m_PropName || prop.m_PropType < PT_STRING || prop.m_PropType > PT_ROTATION)
                    throw std::runtime_error("invalid native class property");
            }
        }
        // Validate all parent pointers and cycles before traversing or resolving
        // callbacks. SDK tables include their BaseClass and every ancestor.
        for (auto* definition : definitions) {
            std::unordered_set<ClassDef*> chain;
            auto* current = definition;
            while (current) {
                if (!members.contains(current) || !chain.insert(current).second)
                    throw std::runtime_error("invalid or cyclic native class hierarchy");
                current = current->m_ParentClass;
            }
        }
        // Constructor inheritance is implemented by typed SDK members, never
        // byte offsets into ClassDef. Resolve into engine-owned callbacks so
        // loading and unloading do not rewrite the module's definitions.
        for (auto* definition : definitions) {
            auto construct = definition->m_ConstructFn;
            auto destruct = definition->m_DestructFn;
            for (auto* parent = definition->m_ParentClass; parent && (!construct || !destruct); parent = parent->m_ParentClass) {
                if (!construct) construct = parent->m_ConstructFn;
                if (!destruct) destruct = parent->m_DestructFn;
            }
            if (!construct || !destruct) throw std::runtime_error("native game class has no lifecycle callbacks");
            callbacks.emplace(definition, Callbacks{construct, destruct, definition->m_ClassObjectSize, alignment(*definition), definition->m_ClassFlags});
        }
    }
    struct Callbacks { ConstructObjectFn construct; DestructObjectFn destruct; std::size_t size, align; uint32 flags; };
    platform::Module module;
    const ServerModuleApi* api = nullptr;
    ILTServer* server;
    std::vector<ClassDef*> definitions;
    std::unordered_map<std::string, ClassDef*> names;
    std::unordered_map<ClassDef*, Callbacks> callbacks;
};
GameModule::GameModule(const std::filesystem::path& path, ILTServer* server)
    : owner_(std::make_shared<Object::Owner>(path, server)) {}
std::span<ClassDef* const> GameModule::classes() const { return owner_->definitions; }
ClassDef* GameModule::find(std::string_view name) const {
    const auto found = owner_->names.find(key(name));
    return found == owner_->names.end() ? nullptr : found->second;
}
GameModule::Object GameModule::create(ClassDef* definition) const {
    // A constructor may release the GameModule that invoked it.
    const auto owner = owner_;
    const auto found = owner->callbacks.find(definition);
    if (found == owner->callbacks.end()) throw std::runtime_error("class does not belong to this game module");
    if (found->second.flags & CF_NORUNTIME) throw std::runtime_error("editor-only class cannot be instantiated");
    auto* storage = ::operator new(found->second.size, std::align_val_t(found->second.align));
    try { found->second.construct(storage); }
    catch (...) { ::operator delete(storage, std::align_val_t(found->second.align)); throw; }
    Object result;
    result.owner_ = owner;
    result.pointer_ = static_cast<BaseClass*>(storage);
    result.definition_ = definition;
    return result;
}
void GameModule::Object::reset() {
    auto owner = std::move(owner_);
    auto* pointer = std::exchange(pointer_, nullptr);
    auto* definition = std::exchange(definition_, nullptr);
    if (pointer) {
        const auto callbacks = owner->callbacks.at(definition);
        callbacks.destruct(pointer);
        ::operator delete(pointer, std::align_val_t(callbacks.align));
    }
}
GameModule::Object::~Object() { reset(); }
GameModule::Object::Object(Object&& other) noexcept
    : owner_(std::move(other.owner_)), pointer_(std::exchange(other.pointer_, nullptr)), definition_(std::exchange(other.definition_, nullptr)) {}
GameModule::Object& GameModule::Object::operator=(Object&& other) noexcept {
    if (this == &other) return *this;
    Object old(std::move(other));
    std::swap(owner_, old.owner_); std::swap(pointer_, old.pointer_); std::swap(definition_, old.definition_);
    return *this;
}
GameModule::Shell GameModule::create_shell() const {
    Shell result;
    result.owner_ = owner_;
    result.pointer_ = result.owner_->api->create_shell(result.owner_->server);
    if (!result.pointer_) throw std::runtime_error("native server shell creation failed");
    return result;
}
void GameModule::Shell::reset() {
    auto owner = std::move(owner_);
    auto* pointer = std::exchange(pointer_, nullptr);
    if (pointer) owner->api->delete_shell(pointer);
}
GameModule::Shell::~Shell() { reset(); }
GameModule::Shell::Shell(Shell&& other) noexcept
    : owner_(std::move(other.owner_)), pointer_(std::exchange(other.pointer_, nullptr)) {}
GameModule::Shell& GameModule::Shell::operator=(Shell&& other) noexcept {
    if (this == &other) return *this;
    Shell old(std::move(other));
    std::swap(owner_, old.owner_); std::swap(pointer_, old.pointer_);
    return *this;
}
}
