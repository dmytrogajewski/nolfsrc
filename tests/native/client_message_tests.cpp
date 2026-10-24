#include "engine/client/client_message_io.h"
#include "engine/client/client_scene.h"
#include "engine/server/message.h"
#include "engine/server/world_state.h"
#include "engine/portable/resources.h"
#include "platform/system.h"
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* error) { if (!value) throw std::runtime_error(error); }
struct Resources {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-client-message-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Resources> catalogue;
    Resources() {
        const std::string text="Case %1!d!: %2!s!";
        nolf::platform::atomic_write(root/"resources/cres/1033/strings/17.txt",std::as_bytes(std::span(text)));
        catalogue=std::make_shared<nolf::Resources>(root);
    }
    ~Resources() { std::error_code error; std::filesystem::remove_all(root,error); }
};
struct Packet {
    CAbstractIO* owner=nullptr;
    Packet(std::shared_ptr<CMsgObjectIO> context) {
        msg_AllocPacket(&owner,4096,4096); require(owner,"client message packet allocation");
        data().m_Write.SetObjectIO(std::move(context));
    }
    ~Packet() { owner->Release(); }
    ServerMessage& data() { return *static_cast<ServerMessage*>(owner); }
    ILTMessage& sdk() { return data().m_Write; }
};
HOBJECT create(nolf::ClientScene& scene) {
    ObjectCreateStruct input; HOBJECT object=nullptr;
    require(scene.create(input,object)==LT_OK && object,"native message scene object"); return object;
}
void messages(Resources& files) {
    auto scene=std::make_shared<nolf::ClientScene>(); auto engine=std::make_shared<int>(42);
    auto context=nolf::client_message_io(scene,files.catalogue,engine);
    auto* object=create(*scene); require(scene->server_id(object,7)==LT_OK,"native message server ID");
    auto* local=create(*scene); nolf::ClientScene other; auto* foreign=create(other);
    {
        Packet packet(context); auto& sdk=packet.sdk();
        require(sdk.WriteObject(object)==LT_OK && packet.data().m_DataLen==3 && packet.data().m_pData[1]==7 && packet.data().m_pData[2]==0,"native object message did not use retail16-bit server ID");
        require(sdk.WriteObject(local)==LT_OK && sdk.WriteObject(nullptr)==LT_OK && sdk.WriteHStringFormatted(17,42,"Cate")==LT_OK,"native client formatted CRES message write");
        sdk.ResetPos(); HOBJECT read=nullptr;
        require(sdk.ReadObjectFL(read)==LT_OK && read==object && sdk.ReadObjectFL(read)==LT_OK && !read && sdk.ReadObjectFL(read)==LT_OK && !read,"native client object/local/null message read");
        HSTRING text=nullptr; require(sdk.ReadHStringFL(text)==LT_OK && text && std::string(sb_GetStringData(text))=="Case 42: Cate","native client message formatted from wrong resource module"); sb_FreeString(text);
        sdk.ResetPos(); const auto position=packet.data().m_Pos;
        require(std::async(std::launch::async,[&] { HOBJECT value=nullptr; return sdk.ReadObjectFL(value); }).get()==LT_ERROR && packet.data().m_Pos==position,"message context crossed scene owner thread or advanced failed read");
        require(scene->remove(object)==LT_OK && sdk.ReadObjectFL(read)==LT_OK && !read,"message retained a removed native object address");
    }
    {
        Packet invalid(context); const auto length=invalid.data().m_DataLen; uint32 flags=0;
        require(invalid.sdk().WriteObject(foreign)==LT_INVALIDPARAMS && invalid.data().m_DataLen==length && invalid.sdk().GetStatus(flags)==LT_OK && (flags&LMSTAT_WRITEOVERFLOW),"foreign message object changed packet bytes or failed to mark invalid write");
    }
    object=create(*scene); scene->server_id(object,7);
    ILTMessage* nested=nullptr; std::weak_ptr<CMsgObjectIO> retained;
    {
        auto owned=nolf::client_message_io(scene,files.catalogue,engine); retained=owned;
        Packet child(owned),parent(owned);
        require(child.sdk().WriteObject(object)==LT_OK && parent.sdk().WriteMessage(child.sdk())==LT_OK,"owned nested client message write");
        parent.sdk().ResetPos(); require(parent.sdk().ReadMessageFL(nested)==LT_OK && nested,"owned nested client message read");
    }
    require(!retained.expired(),"nested message discarded context when parent was released");
    HOBJECT read=nullptr; require(nested->ReadObjectFL(read)==LT_OK && read==object && nested->Release()==LT_OK && retained.expired(),"nested native message did not retain/release its IO context exactly once");
    Packet retired(context); engine.reset(); scene.reset();
    read=reinterpret_cast<HOBJECT>(1);
    require(retired.sdk().WriteObject(nullptr)==LT_NOTINITIALIZED && retired.sdk().ReadObjectFL(read)==LT_NOTINITIALIZED && !read && retired.sdk().WriteHStringFormatted(17,42,"Cate")==LT_NOTINITIALIZED,"retained message called into retired native engine services");
    CPosRef* reference=reinterpret_cast<CPosRef*>(1); context->GetPosRef(&reference);
    require(!reference,"retired message exposed freed world reference");
}
void worlds(Resources& files,const char* data) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(data);
    auto scene=std::make_shared<nolf::ClientScene>(assets); auto engine=std::make_shared<int>(42);
    unsigned count=0;
    for (const auto& name : assets->names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        require(scene->load_world(name)==LT_OK,"client message world load");
        auto context=nolf::client_message_io(scene,files.catalogue,engine);
        Packet packet(context); LTVector middle;
        {
            const auto world=scene->world_data(); middle=(world->m_vMin+world->m_vMax)*.5f;
            require(packet.sdk().WriteCompPos(middle)==LT_OK && packet.sdk().ResetPos()==LT_OK,"native client world position write");
            LTVector decoded; require(packet.sdk().ReadCompPosFL(decoded)==LT_OK,"native client world position read");
            for (uint32 axis=0;axis<3;++axis)
                require(std::fabs(decoded[axis]-middle[axis])<=std::fabs(world->m_vMax[axis]-world->m_vMin[axis])/65536.f+.01f,"native client compressed position uses different world bounds");
        }
        auto* object=create(*scene); scene->server_id(object,7);
        Packet stale(context); require(stale.sdk().WriteObject(object)==LT_OK && stale.sdk().ResetPos()==LT_OK,"level-bound native object message fixture");
        require(scene->unload_world()==LT_OK,"client message world unload");
        // Reload the SAME file and recycle its network ID. A retained packet
        // from the previous level must not resolve to the replacement object.
        require(scene->load_world(name)==LT_OK,"client message same-level restart");
        object=create(*scene); scene->server_id(object,7); HOBJECT read=reinterpret_cast<HOBJECT>(1);
        require(stale.sdk().ReadObjectFL(read)==LT_NOTINWORLD && !read,"stale client message aliased a replacement level's network ID");
        CPosRef* reference=reinterpret_cast<CPosRef*>(1); context->GetPosRef(&reference);
        require(!reference,"stale client message reused replacement level position bounds");
        require(scene->unload_world()==LT_OK,"client message replacement level unload"); ++count;
    }
    require(count,"no original client message worlds exercised");
    std::cout << "Native client message contexts: " << count << " original world bounds and same-level restarts, stale IDs rejected\n";
}
}
int main(int argc,char** argv) {
    try { Resources resources; messages(resources); if (argc==2) worlds(resources,argv[1]);
        std::cout << "Native client message IDs, resources, ownership, nested contexts and retirement passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
