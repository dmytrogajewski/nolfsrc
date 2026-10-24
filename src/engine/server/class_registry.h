#pragma once
#include "engine/portable/game_module.h"
#include "engine/portable/message_format.h"
#include <unordered_map>
#include <vector>

class CServerMgr;
class LTObject;
class CClassInfo {
public:
    ClassDef* m_pClassDef = nullptr;
    uint16 m_nClassIndex = 0;
    LTObject* m_pStaticObject = nullptr;
    uint32 m_Ticks = 0, m_nUpdated = 0, m_nObjects = 0, m_nFrames = 0;
    uint32 m_TotalTicks = 0, m_MaxTicks = 0, m_bPrinted = 0;
};
using CClassStats = CClassInfo;

class CClassMgr {
    struct Lifetime {
        bool alive = true, loading = false, terminating = false;
        std::uint64_t generation = 0;
    };
    // Outlives every registry member, including callbacks from their destructors.
    std::shared_ptr<Lifetime> lifetime_ = std::make_shared<Lifetime>();
public:
    CClassMgr() = default;
    ~CClassMgr();
    CClassMgr(const CClassMgr&) = delete;
    CClassMgr& operator=(const CClassMgr&) = delete;
    LTBOOL Init(CServerMgr*);
    void Term();
    void configure(std::filesystem::path module, nolf::ResourceModule resources = {});
    LTRESULT load(ILTServer*);
    CClassInfo* find(std::string_view) const;
    ClassDef* definition(std::string_view) const;
    CClassInfo* record(HCLASS) const;
    bool owns(ClassDef*) const;
    bool owns(BaseClass*) const;
    std::shared_ptr<const void> lease() const;
    uint32 message(BaseClass*, uint32 id, void* data, float parameter);
    uint32 object_message(BaseClass*, HOBJECT sender, uint32 id, HMESSAGEREAD);
    template<class Callback> decltype(auto) shell(Callback&& callback) const {
        const auto keep_loaded = lease();
        return callback(*m_pServerShell);
    }
    BaseClass* allocate(ClassDef*);
    void destroy(BaseClass*) noexcept;
    const std::string& error() const { return error_; }

    IServerShell* m_pServerShell = nullptr;
    ClassDef* m_pBaseClass = nullptr;
    CClassStats* m_pClassStats = nullptr;
    int m_nClasses = 0;
    uint32 m_nIndex = 0;
    CServerMgr* m_pServerMgr = nullptr;
    nolf::ResourceModule m_Resources;
private:
    std::filesystem::path module_path_;
    std::unique_ptr<nolf::GameModule> module_;
    std::vector<CClassInfo> records_;
    std::unordered_map<std::string, CClassInfo*> names_;
    std::unordered_map<BaseClass*, nolf::GameModule::Object> objects_;
    nolf::GameModule::Shell shell_;
    std::string error_;
};

ClassDef* sub_10001741(void* registry, char* name);
int cb_LoadModule(CClassMgr*);
