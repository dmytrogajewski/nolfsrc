#include "class_registry.h"
#include "platform/system.h"
#include <stdexcept>

CClassMgr::~CClassMgr() { lifetime_->alive = false; Term(); }
LTBOOL CClassMgr::Init(CServerMgr* owner) {
    const auto life = lifetime_;
    if (!life->alive || life->loading || life->terminating) return LTFALSE;
    Term();
    if (!life->alive) return LTFALSE;
    m_pServerMgr = owner;
    return LTTRUE;
}
void CClassMgr::configure(std::filesystem::path module, nolf::ResourceModule resources) {
    if (!lifetime_->alive || lifetime_->loading || lifetime_->terminating || module_)
        throw std::logic_error("cannot reconfigure an active game module");
    module_path_ = std::move(module); m_Resources = std::move(resources);
}
void CClassMgr::Term() {
    const auto life = lifetime_;
    if (life->terminating) return;
    life->terminating = true;
    ++life->generation;
    struct Finish { std::shared_ptr<Lifetime> life; ~Finish() { life->terminating = false; } } finish{life};
    // Detach the complete generation before invoking game code. A callback can
    // destroy this registry; all retiring storage then belongs to this stack.
    auto module = std::move(module_);
    decltype(records_) records;
    records.swap(records_);
    decltype(objects_) objects;
    objects.swap(objects_);
    auto shell = std::move(shell_);
    for (auto& record : records) {
        if (!record.m_pClassDef) continue;
        auto& slot = record.m_pClassDef->m_pInternal[m_nIndex];
        if (slot == &record) slot = nullptr;
    }
    names_.clear();
    m_pClassStats = nullptr; m_nClasses = 0; m_pBaseClass = nullptr;
    m_pServerShell = nullptr;
    // Local destruction order: shell, objects, records, module, finish. Never
    // access a registry member after the first external destructor runs.
}
LTRESULT CClassMgr::load(ILTServer* server) {
    const auto life = lifetime_;
    if (!life->alive || life->loading || life->terminating) return LT_INVALIDOBJECTDLL;
    if (!objects_.empty()) {
        error_ = "cannot reload a game module with live objects";
        return LT_INVALIDOBJECTDLL;
    }
    life->loading = true;
    struct Finish { std::shared_ptr<Lifetime> life; ~Finish() { life->loading = false; } } finish{life};
    Term();
    if (!life->alive) return LT_INVALIDOBJECTDLL;
    const auto generation = life->generation;
    error_.clear();
    try {
        if (module_path_.empty()) throw std::runtime_error("native object module path is not configured");
        const auto path = module_path_;
        auto module = std::make_unique<nolf::GameModule>(path, server);
        if (!life->alive || life->generation != generation) return LT_INVALIDOBJECTDLL;
        module_ = std::move(module);
        const auto definitions = module_->classes();
        m_pBaseClass = module_->find("BaseClass");
        if (!m_pBaseClass) throw std::runtime_error("game module has no BaseClass");
        bool claimed = false;
        for (uint32 slot = 0; slot != 2; ++slot) {
            bool free = true;
            for (auto* def : definitions) if (def->m_pInternal[slot]) { free = false; break; }
            if (free) { m_nIndex = slot; claimed = true; break; }
        }
        if (!claimed) throw std::runtime_error("game class internal slots are already in use");
        records_.resize(definitions.size());
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            auto& record = records_[i];
            record.m_pClassDef = definitions[i]; record.m_nClassIndex = static_cast<uint16>(i);
            names_.emplace(definitions[i]->m_ClassName, &record);
        }
        for (auto& record : records_) record.m_pClassDef->m_pInternal[m_nIndex] = &record;
        m_pClassStats = records_.data(); m_nClasses = static_cast<int>(records_.size());
        auto shell = module_->create_shell();
        if (!life->alive || life->generation != generation) return LT_INVALIDOBJECTDLL;
        shell_ = std::move(shell); m_pServerShell = shell_.get();
        return LT_OK;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (life->alive && life->generation == generation) {
            error_ = message;
            Term();
        }
        nolf::platform::log("game module", message);
        return LT_INVALIDOBJECTDLL;
    }
}
CClassInfo* CClassMgr::find(std::string_view name) const {
    if (!lifetime_->alive || lifetime_->terminating) return nullptr;
    const auto found = names_.find(std::string(name));
    return found == names_.end() ? nullptr : found->second;
}
ClassDef* CClassMgr::definition(std::string_view name) const {
    auto* record = find(name);
    return record ? record->m_pClassDef : nullptr;
}
CClassInfo* CClassMgr::record(HCLASS handle) const {
    if (!lifetime_->alive || lifetime_->terminating) return nullptr;
    for (const auto& entry : records_)
        if (static_cast<const void*>(&entry) == static_cast<const void*>(handle))
            return const_cast<CClassInfo*>(&entry);
    return nullptr;
}
bool CClassMgr::owns(ClassDef* definition) const {
    if (!lifetime_->alive || lifetime_->terminating) return false;
    for (const auto& entry : records_) if (entry.m_pClassDef == definition) return true;
    return false;
}
bool CClassMgr::owns(BaseClass* object) const { return lifetime_->alive && !lifetime_->terminating && objects_.contains(object); }
std::shared_ptr<const void> CClassMgr::lease() const { return module_ ? module_->lease() : nullptr; }
uint32 CClassMgr::message(BaseClass* object, uint32 id, void* data, float parameter) {
    if (!owns(object)) return 0;
    const auto keep_loaded = lease();
    return object->EngineMessageFn(id,data,parameter);
}
uint32 CClassMgr::object_message(BaseClass* object, HOBJECT sender, uint32 id, HMESSAGEREAD message) {
    if (!owns(object)) return 0;
    const auto keep_loaded = lease();
    return object->ObjectMessageFn(sender,id,message);
}
BaseClass* CClassMgr::allocate(ClassDef* definition) {
    const auto life = lifetime_;
    if (!module_ || !life->alive || life->terminating || !owns(definition)) return nullptr;
    const auto generation = life->generation;
    try {
        auto object = module_->create(definition);
        if (!life->alive || life->generation != generation) return nullptr;
        auto* pointer = object.get();
        objects_.emplace(pointer, std::move(object));
        return pointer;
    } catch (const std::exception& error) { nolf::platform::log("game object", error.what()); return nullptr; }
}
void CClassMgr::destroy(BaseClass* object) noexcept {
    if (!object || !lifetime_->alive || lifetime_->terminating) return;
    auto node = objects_.extract(object);
    if (!node.empty()) node.mapped() = {};
}
ClassDef* sub_10001741(void* context, char* name) {
    return context && name ? static_cast<CClassMgr*>(context)->definition(name) : nullptr;
}
