#include "engine/client/client_cursor.h"
#include "engine/portable/cursor.h"
#include <algorithm>
#include <cstdlib>
#include <thread>
#include <unordered_map>

namespace nolf {
namespace {
template<class F> LTRESULT sdk(F&& callback) noexcept {
    try { return callback(); }
    catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
    catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
    catch (...) { return LT_ERROR; }
}
struct Instance final : ILTCursorInst {
    const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<const platform::CursorShape> shape;
    void* data=nullptr;
    bool thread() const { return owner==std::this_thread::get_id(); }
    LTRESULT IsValid() override { return thread() && shape && data ? LT_YES : LT_NO; }
    // SDK data is an opaque application token. Never interpret it as an OS handle.
    void SetData(const void* value) override { if (thread() && shape) data=const_cast<void*>(value); }
    void* GetData() override { return thread() && shape ? data : nullptr; }
};
}
struct ClientCursor::State {
    std::shared_ptr<const Resources> resources;
    std::shared_ptr<platform::CursorProvider> provider;
    const std::thread::id owner=std::this_thread::get_id();
    // Retired instances remain addressable until engine shutdown, so an old
    // SDK handle cannot alias a new cursor or dereference freed memory.
    std::unordered_map<HLTCURSOR,std::unique_ptr<Instance>> instances;
    HLTCURSOR selected=nullptr;
    CursorMode mode=CM_None;
    bool thread() const { return owner==std::this_thread::get_id(); }
    Instance* find(HLTCURSOR value) const {
        const auto it=instances.find(value); return it==instances.end() ? nullptr : it->second.get();
    }
};
ClientCursor::ClientCursor(std::shared_ptr<const Resources> resources,std::shared_ptr<platform::CursorProvider> provider) : state_(std::make_unique<State>()) {
    if (!resources || !provider) throw std::invalid_argument("native cursor services are required");
    state_->resources=std::move(resources); state_->provider=std::move(provider);
    state_->provider->visible(false);
}
ClientCursor::~ClientCursor() { try { state_->provider->select({}); } catch (...) {} }
LTRESULT ClientCursor::SetCursorMode(CursorMode mode) {
    return sdk([&]() -> LTRESULT {
        if (!state_->thread()) return LT_NOTINITIALIZED;
        if (mode!=CM_None && mode!=CM_Hardware) return LT_UNSUPPORTED;
        if (mode==CM_Hardware && !state_->provider->available()) return LT_UNSUPPORTED;
        state_->provider->visible(mode==CM_Hardware); state_->mode=mode; return LT_OK;
    });
}
LTRESULT ClientCursor::GetCursorMode(CursorMode& mode) { if (!state_->thread()) return LT_NOTINITIALIZED; mode=state_->mode; return LT_OK; }
LTRESULT ClientCursor::IsCursorModeAvailable(CursorMode mode) {
    return sdk([&]() -> LTRESULT { if (!state_->thread()) return LT_NOTINITIALIZED; return mode==CM_None || (mode==CM_Hardware && state_->provider->available()) ? LT_YES : LT_NO; });
}
LTRESULT ClientCursor::load(std::string_view name,HLTCURSOR& output) {
    output=nullptr;
    return sdk([&]() -> LTRESULT {
        if (!state_->thread()) return LT_NOTINITIALIZED;
        if (state_->instances.size()>=65536) return LT_OUTOFMEMORY;
        std::vector<CursorEntry> entries;
        try { entries=decode_cursor_group(state_->resources->raw("cres","12",name)); }
        catch (const std::bad_alloc&) { throw; }
        catch (...) { return LT_MISSINGCURSORRESOURCE; }
        std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
            const auto distance=[](const auto& value) { return std::abs(int(value.width)-32)+std::abs(int(value.height/2)-32); };
            const auto da=distance(a),db=distance(b); return da!=db ? da<db : a.depth>b.depth;
        });
        for (const auto& entry : entries) {
            std::shared_ptr<const platform::CursorShape> shape;
            try {
                const auto bytes=state_->resources->raw("cres","1",std::to_string(entry.id));
                if (bytes.size()!=entry.bytes) continue;
                const auto depth=std::to_integer<unsigned>(bytes[18])|(std::to_integer<unsigned>(bytes[19])<<8);
                if (depth!=entry.depth) continue;
                auto bitmap=decode_cursor(bytes);
                if (bitmap.width!=entry.width || bitmap.height*2!=entry.height) continue;
                shape=state_->provider->create(bitmap);
            } catch (const std::bad_alloc&) { throw; }
              catch (const std::exception&) { continue; }
            if (!shape) continue;
            auto instance=std::make_unique<Instance>(); instance->shape=std::move(shape); instance->data=const_cast<platform::CursorShape*>(instance->shape.get());
            const auto handle=instance.get(); state_->instances.emplace(handle,std::move(instance)); output=handle; return LT_OK;
        }
        return LT_MISSINGCURSORRESOURCE;
    });
}
LTRESULT ClientCursor::LoadCursorBitmapResource(uint32 id,HLTCURSOR& cursor) { return load(std::to_string(id),cursor); }
LTRESULT ClientCursor::LoadCursorBitmapResource(const char* name,HLTCURSOR& cursor) {
    cursor=nullptr; if (!name || !*name) return LT_INVALIDPARAMS;
    std::string_view key(name); if (key.front()=='#') key.remove_prefix(1);
    if (key.empty() || key.size()>256 || key.find_first_of("/\\:")!=key.npos) return LT_INVALIDPARAMS;
    return load(key,cursor);
}
LTRESULT ClientCursor::IsValidCursor(HLTCURSOR cursor) {
    if (!state_->thread()) return LT_NOTINITIALIZED;
    const auto* instance=state_->find(cursor); return instance && instance->shape && instance->data ? LT_YES : LT_NO;
}
LTRESULT ClientCursor::SetCursor(HLTCURSOR cursor) {
    return sdk([&]() -> LTRESULT {
        if (!state_->thread()) return LT_NOTINITIALIZED;
        auto* instance=state_->find(cursor); if (!instance || instance->IsValid()!=LT_YES) return LT_INVALIDPARAMS;
        state_->provider->select(instance->shape); state_->selected=cursor; return LT_OK;
    });
}
LTRESULT ClientCursor::FreeCursor(HLTCURSOR cursor) {
    return sdk([&]() -> LTRESULT {
        if (!state_->thread()) return LT_NOTINITIALIZED;
        auto* instance=state_->find(cursor); if (!instance || !instance->shape) return LT_INVALIDPARAMS;
        if (state_->selected==cursor) { state_->provider->select({}); state_->selected=nullptr; }
        instance->shape.reset(); instance->data=nullptr; return LT_OK;
    });
}
LTRESULT ClientCursor::RefreshCursor() {
    return sdk([&]() -> LTRESULT {
        if (!state_->thread()) return LT_NOTINITIALIZED;
        const auto* selected=state_->find(state_->selected);
        state_->provider->select(selected && selected->data ? selected->shape : nullptr);
        state_->provider->visible(state_->mode==CM_Hardware); return LT_OK;
    });
}
}
