#include "model_cache.h"
#include "commonmodel.h"
#include "model_loading.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "platform/filesystem.h"
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

struct ModelCache::Impl {
    explicit Impl(Resolver value) : resolve(std::move(value)) {
        if (!resolve) throw std::invalid_argument("Model cache requires an asset resolver");
    }
    Resolver resolve;
    mutable std::recursive_mutex mutex;
    std::map<std::string,std::weak_ptr<Model>> models;
    std::set<std::string> loading;
    struct Context { Impl* cache; Model* parent; std::string name; std::string error; };
    static LTRESULT child(CModelLoadRequest* request,Model** output) {
        *output = nullptr;
        auto& context = *static_cast<Context*>(request->m_UserData);
        try {
            auto directory = context.name.substr(0,context.name.find_last_of('/') + 1);
            auto model = context.cache->load(directory + request->m_pChildName);
            if (context.parent->m_nChildModels >= 16) return LT_INVALIDMODELFILE;
            context.parent->m_ChildOwners[context.parent->m_nChildModels] = model;
            *output = model.get();
            return LT_OK;
        } catch (const std::exception& error) {
            context.error = error.what();
            return LT_INVALIDMODELFILE;
        }
    }
    std::shared_ptr<Model> load(std::string_view name) {
        const auto key = nolf::platform::asset_key(name);
        if (auto found = models.find(key); found != models.end())
            if (auto live = found->second.lock()) return live;
        if (!loading.insert(key).second) throw std::runtime_error("Cyclic model dependency: " + key);
        struct LoadingGuard {
            std::set<std::string>& entries;
            const std::string& name;
            ~LoadingGuard() { entries.erase(name); }
        } guard{loading,key};
        auto model = std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc);
        const auto source = resolve(key);
        nolf::AssetStream stream(source.asset);
        Context context{this,model.get(),key,{}};
        CModelLoadRequest request;
        request.m_pStream = &stream; request.m_pfnLoadChild = child; request.m_UserData = &context;
        if (model->Load(&stream,&g_DefAlloc) != LT_OK || model->LoadChildModels(&request) != LT_OK ||
            !request.m_bTreesValid || !request.m_bChildrenFound || stream.ErrorStatus() != LT_OK)
            throw std::runtime_error("Cannot load model " + key + (context.error.empty() ? "" : ": " + context.error));
        auto filename = key;
        if (!model->SetFilename(filename.data())) throw std::bad_alloc();
        model->m_FileFlags = source.file_id;
        models[key] = model;
        return model;
    }
};
ModelCache::ModelCache(Resolver resolve) : impl_(std::make_unique<Impl>(std::move(resolve))) {}
ModelCache::ModelCache(std::shared_ptr<const nolf::Assets> assets)
    : ModelCache([assets](std::string_view name) { return Source{assets->locate(name),0xffff}; }) {
    if (!assets) throw std::invalid_argument("Model cache requires mounted assets");
}
ModelCache::~ModelCache() = default;
std::shared_ptr<Model> ModelCache::Load(std::string_view name) {
    std::lock_guard lock(impl_->mutex);
    return impl_->load(name);
}
void ModelCache::Prune() {
    std::lock_guard lock(impl_->mutex);
    std::erase_if(impl_->models,[](const auto& entry) { return entry.second.expired(); });
}
std::size_t ModelCache::Entries() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->models.size();
}

void ModelCache::Invalidate(std::string_view name) {
    const auto key = nolf::platform::asset_key(name);
    std::lock_guard lock(impl_->mutex);
    impl_->models.erase(key);
}
void ModelCache::Clear() {
    std::lock_guard lock(impl_->mutex);
    impl_->models.clear();
}
