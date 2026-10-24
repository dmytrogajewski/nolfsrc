#include "engine/client/client_message_io.h"
#include "engine/client/client_scene.h"
#include "engine/portable/message_format.h"
#include "engine/server/message.h"
#include "engine/server/world_state.h"
#include <thread>
#include <stdexcept>

namespace nolf {
namespace {
class ClientMessageIO final : public CMsgObjectIO {
public:
    ClientMessageIO(std::weak_ptr<ClientScene> scene,std::shared_ptr<const Resources> resources,
        std::weak_ptr<const void> engine,std::uint32_t language)
        : scene_(std::move(scene)),resources_(std::move(resources)),engine_(std::move(engine)),language_(language) {
        const auto active=scene_.lock();
        if (!active || engine_.expired() || !resources_) throw std::invalid_argument("client message context needs live engine services");
        active->retain();
        const auto world=active->world_data(); world_=world; world_identity_=world.get();
        if (world) { bounds_.m_vMin=world->m_vMin; bounds_.m_vMax=world->m_vMax; bounds_.m_vScale=world->m_v15c; }
    }
    template<class F> LTRESULT call(F&& function) noexcept {
        if (owner_!=std::this_thread::get_id()) return LT_ERROR;
        const auto engine=engine_.lock(); const auto scene=scene_.lock();
        if (!engine || !scene) return LT_NOTINITIALIZED;
        try { return function(*scene); }
        catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
        catch (const std::invalid_argument&) { return LT_INVALIDPARAMS; }
        catch (...) { return LT_ERROR; }
    }
    LTRESULT ReadObject(LMessageImpl* message,HOBJECT* object) override {
        if (!object) return LT_INVALIDPARAMS;
        *object=nullptr; if (!message) return LT_INVALIDPARAMS;
        return mark(message,call([&](ClientScene& scene) -> LTRESULT {
            if (!level_matches(scene)) return message->FlagError(LMSTAT_READOVERFLOW,LT_NOTINWORLD);
            uint16 id=0xffff; const auto status=message->ReadWordFL(id);
            if (status==LT_OK && id!=0xffff) *object=scene.find_server_object(id);
            return status;
        }),LMSTAT_READOVERFLOW);
    }
    LTRESULT WriteObject(LMessageImpl* message,HOBJECT object) override {
        if (!message) return LT_INVALIDPARAMS;
        return mark(message,call([&](ClientScene& scene) -> LTRESULT {
            if (!level_matches(scene)) return message->FlagError(LMSTAT_WRITEOVERFLOW,LT_NOTINWORLD);
            uint16 id=0xffff;
            if (object) {
                if (!scene.contains(object)) return LT_INVALIDPARAMS;
                id=scene.server_id(object).value_or(0xffff);
            }
            return message->WriteWord(id);
        }),LMSTAT_WRITEOVERFLOW);
    }
    LTRESULT WriteFormattedHString(LMessageImpl* message,int id,va_list* arguments) override {
        if (!message) return LT_INVALIDPARAMS;
        return mark(message,call([&](ClientScene&) {
            auto text=format_resource({resources_.get(),"cres",language_},id,arguments);
            return message->WriteStringAsHString(text.data());
        }),LMSTAT_WRITEOVERFLOW);
    }
    void GetPosRef(CPosRef** output) override {
        if (!output) return;
        *output=nullptr;
        call([&](ClientScene& scene) -> LTRESULT {
            if (!world_identity_ || !level_matches(scene)) return LT_NOTINWORLD;
            *output=&bounds_; return LT_OK;
        });
    }
private:
    LTRESULT mark(LMessageImpl* message,LTRESULT status,uint32 flag) const {
        // A rejected cross-thread call must not touch the packet's cursor
        // or flags, which belong exclusively to its engine thread.
        return status!=LT_OK && owner_==std::this_thread::get_id() ? message->FlagError(flag,status) : status;
    }
    bool level_matches(ClientScene& scene) const {
        const auto current=scene.world_data();
        if (current.get()!=world_identity_) return false;
        return !world_identity_ || world_.lock()==current;
    }
    const std::thread::id owner_=std::this_thread::get_id();
    std::weak_ptr<ClientScene> scene_;
    std::shared_ptr<const Resources> resources_;
    std::weak_ptr<const void> engine_;
    std::uint32_t language_;
    CPosRef bounds_;
    std::weak_ptr<const CServerWorld> world_;
    const CServerWorld* world_identity_=nullptr;
};
}
std::shared_ptr<CMsgObjectIO> client_message_io(std::weak_ptr<ClientScene> scene,
    std::shared_ptr<const Resources> resources,std::weak_ptr<const void> owner,std::uint32_t language) {
    return std::make_shared<ClientMessageIO>(std::move(scene),std::move(resources),std::move(owner),language);
}
}
