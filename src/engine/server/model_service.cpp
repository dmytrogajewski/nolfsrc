#include "model_service.h"
#include "commonmodel.h"
#include "filemgr.h"
#include <condition_variable>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <limits>
#include <stdexcept>

struct ModelService::Impl {
    explicit Impl(ModelCache::Resolver resolve) : cache(std::move(resolve)), owner(std::this_thread::get_id()) {}
    struct Pending { std::string name; std::shared_ptr<Model> model; bool cancelled = false; uint32 status = LT_INPROGRESS; };
    ModelCache cache;
    std::thread::id owner;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::map<Ticket,Pending> pending;
    std::map<std::string,std::shared_ptr<Model>> resident;
    std::set<std::string> invalidating;
    bool clearing = false;
    Ticket next = 1;
    void Owner() const { if (std::this_thread::get_id() != owner) throw std::logic_error("Model publication requires its owner thread"); }
};
ModelService::ModelService(ModelCache::Resolver resolve) : impl_(std::make_unique<Impl>(std::move(resolve))) {}
ModelService::ModelService(CFileMgr& files) : ModelService([&files](std::string_view name) {
    CFileRef* file = nullptr;
    if (!files.identify(name,0,&file)) throw std::runtime_error("Missing model: " + std::string(name));
    return ModelCache::Source{files.locate(name),file->m_nFileID};
}) {}
ModelService::~ModelService() = default;
ModelService::Ticket ModelService::Reserve(std::string_view name) {
    const auto key = nolf::platform::asset_key(name);
    Ticket ticket;
    {
        std::unique_lock lock(impl_->mutex);
        impl_->condition.wait(lock,[&] { return !impl_->clearing && !impl_->invalidating.contains(key); });
        if (impl_->next == std::numeric_limits<Ticket>::max()) throw std::overflow_error("Model load ticket space exhausted");
        ticket = impl_->next++;
        impl_->pending.emplace(ticket,Impl::Pending{key,{}});
    }
    return ticket;
}
ModelService::Load ModelService::BeginLoad(std::string_view name) { return LoadReserved(Reserve(name)); }
ModelService::Load ModelService::LoadReserved(Ticket ticket) {
    std::string key;
    {
        std::lock_guard lock(impl_->mutex);
        auto found = impl_->pending.find(ticket);
        if (found == impl_->pending.end()) return {ticket,{},LT_NOCHANGE};
        if (found->second.status != LT_INPROGRESS) return {ticket,found->second.model,found->second.status};
        key = found->second.name;
        if (found->second.cancelled) {
            found->second.status = LT_NOCHANGE;
            return {ticket,{},LT_NOCHANGE};
        }
    }
    std::shared_ptr<Model> model;
    uint32 status = LT_OK;
    try { model = impl_->cache.Load(key); }
    catch (const std::bad_alloc&) { status = LT_OUTOFMEMORY; }
    catch (const std::exception&) { status = LT_INVALIDMODELFILE; }
    {
        std::lock_guard lock(impl_->mutex);
        if (auto found = impl_->pending.find(ticket); found != impl_->pending.end()) {
            found->second.model = model;
            found->second.status = status;
        }
    }
    return {ticket,std::move(model),status};
}
std::shared_ptr<Model> ModelService::Complete(Ticket ticket) { return Finish(ticket).model; }
ModelService::Completion ModelService::Finish(Ticket ticket) {
    impl_->Owner();
    Impl::Pending pending;
    std::shared_ptr<Model> replaced;
    {
        std::lock_guard lock(impl_->mutex);
        auto found = impl_->pending.find(ticket);
        if (found == impl_->pending.end() || found->second.status == LT_INPROGRESS) return {};
        if (!found->second.cancelled && found->second.model) {
            // Allocate the resident index before consuming the completion.
            // An allocation failure leaves the owned ticket available to retry.
            auto& resident = impl_->resident[found->second.name];
            replaced = std::move(resident);
            resident = found->second.model;
        }
        pending = std::move(found->second);
        impl_->pending.erase(found);
    }
    return { !pending.cancelled,pending.status,pending.cancelled ? std::shared_ptr<Model>{} : std::move(pending.model) };
}
void ModelService::Discard(Ticket ticket) {
    Impl::Pending discarded;
    {
        std::lock_guard lock(impl_->mutex);
        if (auto found = impl_->pending.find(ticket); found != impl_->pending.end()) {
            discarded = std::move(found->second);
            impl_->pending.erase(found);
        }
    }
}
std::shared_ptr<Model> ModelService::Find(std::string_view name) const {
    const auto key = nolf::platform::asset_key(name);
    std::lock_guard lock(impl_->mutex);
    auto found = impl_->resident.find(key);
    return found == impl_->resident.end() ? std::shared_ptr<Model>{} : found->second;
}
std::shared_ptr<Model> ModelService::Retain(Model* model) const {
    if (!model) return {};
    std::lock_guard lock(impl_->mutex);
    for (const auto& [name,resident] : impl_->resident) if (resident.get() == model) return resident;
    return {};
}
bool ModelService::HasPending(std::string_view name) const {
    const auto key = nolf::platform::asset_key(name);
    std::lock_guard lock(impl_->mutex);
    for (const auto& [ticket,pending] : impl_->pending) if (pending.name == key && !pending.cancelled) return true;
    return false;
}
std::shared_ptr<Model> ModelService::Unload(std::string_view name) {
    impl_->Owner();
    const auto key = nolf::platform::asset_key(name);
    std::shared_ptr<Model> removed;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->invalidating.insert(key);
        for (auto& [ticket,pending] : impl_->pending) if (pending.name == key) pending.cancelled = true;
    }
    try { impl_->cache.Invalidate(key); }
    catch (...) {
        { std::lock_guard lock(impl_->mutex); impl_->invalidating.erase(key); }
        impl_->condition.notify_all();
        throw;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (auto found = impl_->resident.find(key); found != impl_->resident.end()) {
            removed = std::move(found->second);
            impl_->resident.erase(found);
        }
        impl_->invalidating.erase(key);
    }
    impl_->condition.notify_all();
    return removed;
}
std::vector<std::string> ModelService::CollectUnused(const std::function<void(std::string_view)>& retire) {
    impl_->Owner();
    std::vector<std::string> names;
    for (;;) {
        std::vector<std::pair<std::string,std::shared_ptr<Model>>> removed;
        {
            std::lock_guard lock(impl_->mutex);
            for (auto it = impl_->resident.begin(); it != impl_->resident.end();) {
                if (it->second.use_count() != 1 || (it->second->m_Flags & 1)) { ++it; continue; }
                names.push_back(it->first);
                removed.emplace_back(it->first,std::move(it->second));
                it = impl_->resident.erase(it);
            }
        }
        if (removed.empty()) break;
        if (retire) for (const auto& [name,model] : removed) retire(name);
    }
    impl_->cache.Prune();
    return names;
}
void ModelService::Term() {
    impl_->Owner();
    std::map<Ticket,Impl::Pending> pending;
    std::map<std::string,std::shared_ptr<Model>> resident;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->clearing = true;
        pending.swap(impl_->pending);
        resident.swap(impl_->resident);
    }
    impl_->cache.Clear();
    {
        std::lock_guard lock(impl_->mutex);
        impl_->clearing = false;
    }
    impl_->condition.notify_all();
}

std::vector<std::pair<ModelService::Ticket,std::string>> ModelService::Ready() const {
    std::vector<std::pair<Ticket,std::string>> ready;
    std::lock_guard lock(impl_->mutex);
    for (const auto& [ticket,pending] : impl_->pending)
        if (pending.status != LT_INPROGRESS) ready.emplace_back(ticket,pending.name);
    return ready;
}

void ModelService::CancelAll() {
    impl_->Owner();
    std::lock_guard lock(impl_->mutex);
    for (auto& [ticket,pending] : impl_->pending) pending.cancelled = true;
}
