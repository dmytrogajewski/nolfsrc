#include "engine/client/client_scene.h"
#include "engine/server/commonmodel.h"
#include "engine/server/objtypes.h"
#include "engine/server/world_state.h"
#include "engine/server/world_record.h"
#include "engine/server/world_clip.h"
#include "engine/server/worldsim.h"
#include "ltserverobj.h"
#include "platform/logging.h"
#include "engine/portable/wire.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include "ilttransform.h"
#include "iltlightanim.h"
#include "model_fixture.h"
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
unsigned solver_errors=0;
void diagnostic(const char* subsystem,std::size_t subsystem_size,const char* text,std::size_t size) {
    const std::string_view message(text,size);
    if (message.find("physics recursed infinitely")!=std::string_view::npos || message.find("doesn't have PBlocks")!=std::string_view::npos) ++solver_errors;
    std::clog << '[' << std::string_view(subsystem,subsystem_size) << "] " << message << '\n';
}
bool near(LTVector a,LTVector b) { return a.DistSqr(b)<.0001f; }
struct Files {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("nolf-client-scene-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::shared_ptr<nolf::Assets> assets=std::make_shared<nolf::Assets>();
    Files() {
        std::filesystem::create_directories(root/"models");
        nolf::platform::atomic_write(root/"models/simple.abc",nolf_test::tiny_model());
        nolf::platform::atomic_write(root/"models/commands.abc",nolf_test::tiny_model({},"first \"two words\"; second three"));
        assets->mount_directory(root);
    }
    ~Files() { std::error_code error; std::filesystem::remove_all(root,error); }
};
HOBJECT create(nolf::ClientScene& scene,uint16 type=OT_NORMAL,const char* filename="") {
    ObjectCreateStruct create; create.m_ObjectType=type; std::strcpy(create.m_Filename,filename);
    HOBJECT object=nullptr; require(scene.create(create,object)==LT_OK && object,"native scene object creation failed"); return object;
}
void objects_and_cameras() {
    nolf::ClientScene scene,foreign;
    HLIGHTANIM light=0; uint32 frames=99; LAInfo lighting{};
    require(scene.lighting().FindLightAnim("missing",light)==LT_NOTFOUND && light==INVALID_LIGHT_ANIM && scene.lighting().GetNumFrames(light,frames)==LT_INVALIDPARAMS && frames==0,"light animation lookup without a world did not initialize SDK outputs");
    require(scene.lighting().GetLightAnimInfo(light,lighting)==LT_INVALIDPARAMS && lighting.m_iFrames[0]==LIGHTANIMFRAME_NONE && lighting.m_iFrames[1]==LIGHTANIMFRAME_NONE,"invalid client light animation returned uninitialized frames");
    for (uint16 type : {OT_NORMAL,OT_CAMERA,OT_LIGHT,OT_CANVAS,OT_LINESYSTEM,OT_PARTICLESYSTEM,OT_POLYGRID,OT_SPRITE}) {
        auto* object=create(scene,type); uint32 actual=99;
        require(scene.common().GetObjectType(object,&actual)==LT_OK && actual==type,"SDK common type differs from canonical allocated object");
        require(foreign.remove(object)==LT_INVALIDPARAMS && scene.contains(object),"foreign scene removed an object it did not own");
        require(scene.remove(object)==LT_OK && !scene.contains(object),"scene object failed to retire");
    }
    auto* object=create(scene); int user=42;
    require(scene.user_data(object,&user)==LT_OK && scene.info(object)->user==&user,"client metadata narrowed native user data");
    require(scene.position(object,{1,2,3},true)==LT_OK && near(object->m_Pos,{1,2,3}),"native client object position");
    auto position=object->m_Pos; position.x+=.001f;
    require(scene.position(object,position)==LT_OK && object->m_Pos.x==1 && scene.position(object,position,true)==LT_OK && object->m_Pos.x==position.x,"SDK forced small position change");
    require(scene.scale(object,{-2,3,4})==LT_OK && object->m_Scale.x==-2,"native scene discarded a finite mirrored scale");
    require(scene.color(object,-1,.5f,2,1)==LT_OK && object->m_ColorR==0 && object->m_ColorG==127 && object->m_ColorB==255,"object color clamp");
    require(scene.position(object,{std::numeric_limits<float>::quiet_NaN(),0,0},true)==LT_INVALIDPARAMS && near(object->m_Pos,position),"nonfinite object movement committed");
    auto* camera=create(scene,OT_CAMERA); auto projection=scene.camera(camera);
    require(projection && projection->fullscreen && projection->rectangle.right==640,"camera default viewport");
    require(scene.viewport(1280,720)==LT_OK && scene.camera(camera)->rectangle.bottom==720,"fullscreen camera did not follow drawable resize");
    projection->fullscreen=false; projection->rectangle={100,20,500,320}; projection->fov_x=1.2f;
    require(scene.camera(camera,*projection)==LT_OK && scene.camera(camera)->rectangle.left==100 && static_cast<CObjCamera*>(camera)->m_fFovX==1.2f,"native camera and viewport metadata disagree");
    projection->rectangle={std::numeric_limits<int>::min(),0,std::numeric_limits<int>::max(),10};
    require(scene.camera(camera,*projection)==LT_INVALIDPARAMS && scene.camera(camera)->rectangle.left==100,"camera rectangle overflow was accepted");
    auto* other=create(scene);
    require(scene.server_id(object,7)==LT_OK && scene.find_server_object(7)==object && object->m_ObjectID==7,"server ID did not map to native client identity");
    require(scene.server_id(other,7)==LT_ALREADYEXISTS && scene.find_server_object(7)==object && !scene.info(other)->server_id,"duplicate server ID changed existing mapping");
    require(scene.server_id(object,11)==LT_OK && !scene.find_server_object(7) && scene.find_server_object(11)==object,"server ID replacement left stale mapping");
    require(scene.remove(object)==LT_OK && !scene.find_server_object(11),"object removal retained a network ID");
    ILTMessage* message=nullptr;
    require(scene.common().CreateMessage(message)==LT_OK && message && message->WriteDWord(0x12345678)==LT_OK && message->ResetPos()==LT_OK,"client common data message allocation/write");
    uint32 word=0; require(message->ReadDWordFL(word)==LT_OK && word==0x12345678 && message->Release()==LT_OK,"client data message framing/release differs from SDK");
    auto& api=scene.common();
    require(std::async(std::launch::async,[&] { uint32 type=0; return api.GetObjectType(other,&type); }).get()==LT_ERROR,"SDK common call crossed the owning scene thread");
    require(scene.clear()==LT_OK && scene.size()==0,"scene clear did not release every owned type");
}
void callbacks() {
    auto scene=std::make_unique<nolf::ClientScene>(); auto* object=create(*scene); const auto original=object->m_Lifetime;
    scene->client_flags(object,CF_NOTIFYREMOVE);
    unsigned removed=0; HOBJECT created=nullptr;
    nolf::ClientSceneEvents events;
    events.removed=[&](HOBJECT handle) {
        ++removed; require(scene->contains(handle) && scene->info(handle)->removing,"removal callback lost its still-readable object");
        require(scene->remove(handle)==LT_NOCHANGE,"recursive removal did not stop at pending identity");
        created=create(*scene);
    }; scene->events(events);
    require(scene->remove(object)==LT_OK && removed==1 && !*original && scene->contains(created) && scene->size()==1,"removal callback invalidated new objects or repeated old callback");
    scene->client_flags(created,CF_NOTIFYREMOVE);
    events.removed=[&](HOBJECT) { ObjectCreateStruct input; HOBJECT output=reinterpret_cast<HOBJECT>(1); require(scene->create(input,output)==LT_NOTINITIALIZED && !output,"clear callback published a new object"); };
    scene->events(events); require(scene->clear()==LT_OK && scene->size()==0,"callback-safe bulk clear failed");
    object=create(*scene); scene->client_flags(object,CF_NOTIFYREMOVE);
    events.removed=[](HOBJECT) { throw std::runtime_error("fixture callback failed"); }; scene->events(events);
    require(scene->remove(object)==LT_ERROR && !scene->contains(object),"throwing game callback left a half-removed native object");

    struct CopyFailure {
        bool& armed;
        explicit CopyFailure(bool& flag) : armed(flag) {}
        CopyFailure(const CopyFailure& other) : armed(other.armed) { if (armed) throw std::bad_alloc(); }
        void operator()(HOBJECT) const {}
    };
    bool armed=false; object=create(*scene); scene->client_flags(object,CF_NOTIFYREMOVE); scene->server_id(object,19);
    events={}; events.removed=CopyFailure(armed); scene->events(std::move(events)); armed=true;
    require(scene->remove(object)==LT_OUTOFMEMORY && !scene->info(object)->removing && scene->find_server_object(19)==object,"callback copy allocation failure partially retired a client object");
    armed=false; require(scene->remove(object)==LT_OK,"client object removal did not recover after callback allocation failure");

    object=create(*scene); auto* trigger=create(*scene); object->m_Flags=FLAG_SOLID|FLAG_TOUCH_NOTIFY; trigger->m_Flags=FLAG_TOUCH_NOTIFY;
    scene->position(object,{-10,0,0},true); scene->position(trigger,{0,0,0},true);
    auto& physics=scene->physics(); const auto lifetime=object->m_Lifetime;
    bool touched=false; events={}; events.touch=[&](HOBJECT,const CollisionInfo&,float) { touched=true; scene.reset(); };
    scene->events(events); HOBJECT list[]{trigger}; LTVector target{10,0,0};
    require(physics.MovePushObjects(object,target,list,1)==LT_OK && touched && !scene && !*lifetime,"physics did not retain scene state through callback host retirement");
}
void attachments() {
    nolf::ClientScene scene; auto* parent=create(scene); auto* child=create(scene); auto* grandchild=create(scene);
    LTRotation identity; identity.Init(); HATTACHMENT first=nullptr,second=nullptr;
    require(scene.attach(parent,child,-1,{2,0,0},identity,first)==LT_OK && first && near(child->m_Pos,{2,0,0}),"native attachment initial placement");
    require(scene.attach(child,grandchild,-1,{0,3,0},identity,second)==LT_OK && scene.position(parent,{10,20,30},true)==LT_OK && near(child->m_Pos,{12,20,30}) && near(grandchild->m_Pos,{12,23,30}),"recursive client attachments did not follow canonical physics movement");
    HOBJECT owner=nullptr,dependent=nullptr;
    require(scene.common().GetAttachmentObjects(first,owner,dependent)==LT_OK && owner==parent && dependent==child,"actual common attachment virtual dispatch");
    HATTACHMENT cycle=nullptr; require(scene.attach(grandchild,parent,-1,{0,0,0},identity,cycle)==LT_ERROR && !cycle,"attachment cycle accepted");
    require(scene.remove(child)==LT_OK && scene.common().GetAttachmentObjects(first,owner,dependent)==LT_INVALIDPARAMS && scene.contains(grandchild),"child removal retained incoming/outgoing attachment identity");

    nolf::ClientScene deep; parent=create(deep); auto* tail=parent;
    constexpr unsigned depth=512;
    for (unsigned i=0;i<depth;++i) {
        auto* next=create(deep); HATTACHMENT link=nullptr;
        require(deep.attach(tail,next,-1,{1,0,0},identity,link)==LT_OK,"deep native attachment setup"); tail=next;
    }
    require(deep.position(parent,{10,20,30},true)==LT_OK && near(tail->m_Pos,{float(10+depth),20,30}),"deep client attachment chain did not update without recursive movement");
}
void models(Files& files) {
    nolf::ClientScene scene(files.assets),foreign(files.assets);
    auto* object=create(scene,OT_MODEL,"MODELS\\simple.ABC");
    require(scene.info(object)->filename=="models/simple.abc","model filename ownership/case folding");
    LTAnimTracker* main=nullptr; require(scene.model().GetMainTracker(object,main)==LT_OK && main && main->GetModelInstance()==object,"scene model interface does not address the actual native instance");
    HMODELNODE node=INVALID_MODEL_NODE; char root[]="root";
    require(scene.model().GetNode(object,root,node)==LT_OK && node==0,"original-format ABC model node lookup");
    LTAnimTracker external;
    require(scene.model().AddTracker(object,&external)==LT_OK && external.GetModelInstance()==object,"scene-owned secondary tracker index");
    auto* other=create(foreign,OT_MODEL,"models/simple.abc");
    require(foreign.model().AddTracker(other,&external)==LT_INVALIDPARAMS && external.GetModelInstance()==object,"foreign scene stole an animation tracker");
    require(scene.model().RemoveTracker(object,&external)==LT_OK && !external.GetModelInstance() && scene.model().GetCurAnim(&external,node)==LT_INVALIDPARAMS,"removed tracker retained SDK ownership");
    ObjectCreateStruct replacement; std::strcpy(replacement.m_Filename,"models/missing.abc");
    const auto old_model=static_cast<ModelInstance*>(object)->m_ModelOwner;
    require(scene.common().SetObjectFilenames(object,&replacement)==LT_INVALIDMODELFILE && static_cast<ModelInstance*>(object)->m_ModelOwner==old_model,"failed model replacement discarded the live model");
    std::strcpy(replacement.m_Filename,"models/commands.abc");
    require(scene.common().SetObjectFilenames(object,&replacement)==LT_OK && scene.info(object)->filename=="models/commands.abc","model replacement did not commit canonical resources");
    scene.client_flags(object,CF_NOTIFYMODELKEYS); scene.model().ResetAnim(main);
    std::vector<std::vector<std::string>> commands;
    nolf::ClientSceneEvents events;
    events.model_key=[&](HOBJECT key_object,ArgList& args) {
        require(key_object==object,"model string key lost native object identity"); std::vector<std::string> words;
        for (int i=0;i<args.argc;++i) words.emplace_back(args.argv[i]); commands.push_back(std::move(words));
    }; scene.events(events);
    require(scene.update(.001f)==LT_OK && commands==std::vector<std::vector<std::string>>{{"first","two words"},{"second","three"}},"actual model-key ArgList did not preserve quoting and command boundaries");
    commands.clear(); scene.model().ResetAnim(main);
    events.model_key=[&](HOBJECT key_object,ArgList&) { commands.emplace_back(); require(scene.remove(key_object)==LT_OK,"model callback object removal"); }; scene.events(events);
    require(scene.update(.001f)==LT_OK && commands.size()==1 && !scene.contains(object),"model callback continued through a freed model or keyframe string");

    auto owner=std::make_unique<nolf::ClientScene>(files.assets); auto* animated=create(*owner,OT_MODEL,"models/commands.abc"); owner->client_flags(animated,CF_NOTIFYMODELKEYS);
    auto& api=owner->model(); const auto lifetime=animated->m_Lifetime;
    events={}; events.model_key=[&](HOBJECT,ArgList&) { owner.reset(); }; owner->events(events);
    require(api.UpdateMainTracker(animated,.001f)==LT_OK && !owner && !*lifetime,"direct SDK model callback failed to retain retiring scene state");

    owner=std::make_unique<nolf::ClientScene>(files.assets); animated=create(*owner,OT_MODEL,"models/simple.abc");
    auto secondary=std::make_unique<LTAnimTracker>();
    require(owner->model().AddTracker(animated,secondary.get())==LT_OK,"retiring object secondary tracker setup");
    owner->client_flags(animated,CF_NOTIFYREMOVE); const auto removing_lifetime=animated->m_Lifetime;
    events={}; events.removed=[&](HOBJECT) { secondary.reset(); owner.reset(); }; owner->events(events);
    require(owner->remove(animated)==LT_OK && !owner && !*removing_lifetime,"removal touched game-owned tracker storage after host retirement");

    owner=std::make_unique<nolf::ClientScene>(files.assets); animated=create(*owner,OT_MODEL,"models/commands.abc"); owner->client_flags(animated,CF_NOTIFYMODELKEYS);
    nolf::ClientSceneEvents leased; leased.code=std::make_shared<int>(42); std::weak_ptr<const void> code=leased.code;
    leased.model_key=[&](HOBJECT,ArgList&) { owner.reset(); require(!code.expired(),"host retirement released callback code before the callback returned"); };
    owner->events(std::move(leased)); auto& leased_api=owner->model();
    require(leased_api.UpdateMainTracker(animated,.001f)==LT_OK && !owner && code.expired(),"callback code lease survived beyond the final active dispatch");
}
void node_callbacks(Files& files) {
    auto owner=std::make_unique<nolf::ClientScene>(files.assets); auto* object=create(*owner,OT_MODEL,"models/simple.abc");
    auto control=[](HOBJECT,HMODELNODE,LTMatrix*,void* user) { (*static_cast<std::function<void()>*>(user))(); };
    std::function<void()> callback=[&] { require(owner->remove(object)==LT_OK,"node callback object removal"); };
    require(owner->node_control(object,control,&callback)==LT_OK,"node callback binding");
    LTransform out;
    require(owner->model().GetNodeTransform(object,0,out,LTTRUE)==LT_ERROR && !owner->contains(object),"node evaluation continued after callback removed its model");
    object=create(*owner,OT_MODEL,"models/simple.abc");
    callback=[&] { ObjectCreateStruct replacement; std::strcpy(replacement.m_Filename,"models/commands.abc"); require(owner->filenames(object,replacement)==LT_OK,"node callback model replacement"); };
    owner->node_control(object,control,&callback);
    require(owner->model().GetSocketTransform(object,0,out,LTTRUE)==LT_ERROR && owner->contains(object),"socket evaluation used old data after callback rebound its model");
    auto* child=create(*owner); LTRotation identity; identity.Init(); HATTACHMENT attachment=nullptr;
    callback=[] { throw std::runtime_error("fixture node callback failed"); };
    require(owner->attach(object,child,0,{0,0,0},identity,attachment)==LT_ERROR && !attachment,"throwing attachment callback returned a published handle");
    uint32 size=99,total=99;
    require(owner->common().GetAttachments(object,nullptr,0,size,total)==LT_OK && size==0 && total==0,"throwing attachment callback left an unreachable attachment");
    owner->node_control(object,nullptr,nullptr);
    require(owner->attach(object,child,0,{0,0,0},identity,attachment)==LT_OK,"node attachment setup");
    callback=[&] { owner.reset(); }; owner->node_control(object,control,&callback);
    auto& api=owner->common(); const auto lifetime=object->m_Lifetime;
    require(api.GetAttachmentTransform(attachment,out,LTTRUE)==LT_ERROR && !owner && !*lifetime,"SDK attachment transform did not pin retiring scene through node callback");
}
void sdk_objects(Files& files) {
    const auto& api=nolf::ClientScene::functions(); ObjectCreateStruct input;
#define NOLF_SCENE_API(name) require(api.name!=nullptr,"missing scene SDK function: " #name);
#include "engine/client/client_scene_members.inc"
#undef NOLF_SCENE_API
    require(!api.CreateObject(&input),"scene SDK created an object outside a callback scope");
    LTVector scale{1,2,3}; require(api.SetObjectScale(nullptr,&scale)==LT_NOTINITIALIZED,"scene SDK did not report missing callback context");
    nolf::ClientScene scene(files.assets),foreign;
    HOBJECT object=nullptr;
    {
        const auto active=scene.activate(); object=api.CreateObject(&input); require(object && scene.contains(object),"SDK CreateObject did not use the active scene");
        int user=42; api.SetObjectUserData(object,&user);
        require(api.GetObjectUserData(object)==&user && api.SetObjectScale(object,&scale)==LT_OK,"SDK native metadata/scale");
        LTRotation rotation; rotation.Init(); LTVector position{10,20,30}; api.SetObjectPosAndRotation(object,&position,&rotation);
        LTVector actual; api.GetObjectPos(object,&actual); require(near(actual,position),"SDK object transform did not reach the native instance");
        auto invalid=rotation; invalid.m_Quat[0]=std::numeric_limits<float>::quiet_NaN(); LTVector rejected{40,50,60}; api.SetObjectPosAndRotation(object,&rejected,&invalid);
        require(near(object->m_Pos,position),"SDK combined transform partially committed invalid rotation");
        api.SetObjectColor(object,-1,.5f,2,1); float green=0,alpha=0; api.GetObjectColor(object,nullptr,&green,nullptr,&alpha);
        require(std::fabs(green-127/255.f)<.0001f && alpha==1,"SDK color conversion/null optional outputs");
        require(api.SetObjectUserFlags(object,0xfedcba98)==LT_OK && scene.server_id(object,3)==LT_OK && api.SetObjectUserFlags(object,7)==LT_ERROR,"SDK allowed changing server-owned user flags");
        uint32 flags=0; require(api.GetObjectUserFlags(object,&flags)==LT_OK && flags==0xfedcba98,"SDK user flag width/framing");
        api.SetObjectClientFlags(object,0x12345678); require(api.GetObjectClientFlags(object)==0x12345678,"SDK client flags narrowed a 32-bit value");
        {
            const auto nested=foreign.activate();
            require(api.SetObjectScale(object,&scale)==LT_INVALIDPARAMS,"nested SDK scope dereferenced another scene's object");
            auto* temporary=api.CreateObject(&input); require(foreign.contains(temporary) && !scene.contains(temporary),"nested SDK context selected wrong scene");
        }
        require(api.GetObjectUserData(object)==&user,"nested scene callback did not restore SDK context");
        require(std::async(std::launch::async,[&] { return api.SetObjectScale(object,&scale); }).get()==LT_NOTINITIALIZED,"scene SDK context leaked into another thread");
        input.m_ObjectType=OT_CAMERA; auto* camera=api.CreateObject(&input);
        LTVector point; require(api.Get3DCameraPt(camera,320,240,&point)==LT_OK && near(point,{0,0,1}) && api.Get3DCameraPt(camera,640,240,&point)==LT_OUTSIDE,"SDK camera point projection/half-open rectangle");
        api.SetCameraFOV(camera,-10,10); float x=0,y=0; api.GetCameraFOV(camera,&x,&y);
        require(x>0 && x<.032f && y>3.1f && y<3.142f,"SDK camera FOV clamp");
        api.SetCameraFOV(camera,std::numeric_limits<float>::quiet_NaN(),1); float unchanged=0; api.GetCameraFOV(camera,&unchanged,nullptr); require(unchanged==x,"nonfinite SDK FOV changed camera");
        api.SetCameraFOV(camera,1.5707964f,1.5707964f); api.SetCameraRect(camera,LTFALSE,0,0,1,1);
        require(api.Get3DCameraPt(camera,0,0,&point)==LT_OK && std::isfinite(point.x) && std::isfinite(point.y),"one-pixel camera divided by zero");
        api.SetCameraRect(camera,LTTRUE,0,0,0,0); require(scene.viewport(1024,768)==LT_OK,"SDK camera resize setup");
        LTBOOL fullscreen=LTFALSE; int right=0,bottom=0; api.GetCameraRect(camera,&fullscreen,nullptr,nullptr,&right,&bottom);
        require(fullscreen && right==1024 && bottom==768,"SDK fullscreen rectangle did not ignore supplied empty rect or follow resize");
        LTVector add{.1f,.2f,.3f}; require(api.SetCameraLightAdd(camera,&add) && api.GetCameraLightAdd(camera,&point) && near(point,add) && !api.SetCameraLightAdd(object,&add),"SDK camera light add/type validation");
        input.m_ObjectType=OT_MODEL; std::strcpy(input.m_Filename,"models/simple.abc"); auto* model=api.CreateObject(&input);
        HMODELNODE node=INVALID_MODEL_NODE; char name[3]{};
        require(api.GetNextModelNode(model,INVALID_MODEL_NODE,&node)==LT_OK && node==0 && api.GetModelNodeName(model,node,name,sizeof(name))==LT_OK && std::string(name)=="ro" && api.GetNextModelNode(model,node,&node)==LT_FINISHED,"SDK model node iteration/name termination");
    }
    require(api.GetObjectUserData(object)==nullptr,"SDK object call reused context after scope ended");
    auto retiring=std::make_unique<nolf::ClientScene>(); object=create(*retiring); const auto lifetime=object->m_Lifetime;
    nolf::ClientSceneEvents leased; leased.code=std::make_shared<int>(42); std::weak_ptr<const void> code=leased.code; retiring->events(std::move(leased));
    {
        const auto active=retiring->activate(); retiring.reset();
        require(*lifetime && !code.expired() && api.SetObjectScale(object,&scale)==LT_NOTINITIALIZED && !api.CreateObject(&input),"retired scene SDK scope released callback storage/code or accepted new work");
    }
    require(!*lifetime && code.expired(),"retired scene retained objects/code after its final SDK callback scope");
}
void original_assets(const char* path) {
    auto assets=std::make_shared<nolf::Assets>(); assets->mount_game(path); nolf::ClientScene scene(assets);
    auto* camera=create(scene,OT_CAMERA); unsigned worlds=0,models=0,starts=0,sweeps=0,contacts=0,visibility_rejections=0,light_animations=0;
    for (const auto& name : assets->names()) {
        if (name.ends_with(".abc")) {
            auto* object=create(scene,OT_MODEL,name.c_str()); LTAnimTracker* tracker=nullptr;
            require(scene.model().GetMainTracker(object,tracker)==LT_OK && tracker->GetModel(),"original ABC did not bind to scene model interface");
            LTransform transform; require(scene.model().GetNodeTransform(object,0,transform,LTTRUE)==LT_OK,"original model root transform failed through scene SDK");
            require(scene.remove(object)==LT_OK,"original model scene release failed"); ++models;
        }
        if (name.starts_with("worlds/") && name.ends_with(".dat")) {
            require(scene.load_world(name)==LT_OK && scene.main_world() && scene.contains(camera),"original client scene world load discarded persistent camera");
            const auto world=scene.main_world(); auto info=scene.info(world); require(info && info->main_world && info->lifetime==nolf::ClientObjectLifetime::World,"main world scene metadata");
            const auto data=scene.world_data(); auto* geometry=static_cast<CWorldBsp*>(world->m_pWorldBsp);
            for (uint32 index=0;index<data->m_Anims.GetSize();++index) {
                const auto& animation=data->m_Anims[index]; HLIGHTANIM light=INVALID_LIGHT_ANIM; uint32 frames=0; LAInfo original{};
                require(scene.lighting().FindLightAnim(animation.m_Name,light)==LT_OK && light==index && scene.lighting().GetNumFrames(light,frames)==LT_OK && frames==animation.m_nFrames && scene.lighting().GetLightAnimInfo(light,original)==LT_OK,"original client light animation did not bind to native world records");
                auto changed=original; changed.m_bShadowMap=!original.m_bShadowMap;
                changed.m_iFrames[0]=changed.m_iFrames[1]=frames ? frames-1 : LIGHTANIMFRAME_NONE;
                changed.m_fPercentBetween=.375f; changed.m_fBlendPercent=.5f;
                if (original.m_bShadowMap) { changed.m_vLightPos.Init(1,2,3); changed.m_vLightColor.Init(20,40,60); changed.m_fLightRadius=100; }
                require(scene.lighting().SetLightAnimInfo(light,changed)==LT_OK,"original client light animation state update");
                LAInfo actual{}; require(scene.lighting().GetLightAnimInfo(light,actual)==LT_OK && actual.m_bShadowMap==original.m_bShadowMap && actual.m_iFrames[0]==changed.m_iFrames[0] && actual.m_fBlendPercent==.5f && data->m_Anims[index].m_fBlendPercent==.5f,"client light setter changed immutable shadow mode or missed renderer world data");
                auto invalid=changed; invalid.m_fPercentBetween=std::numeric_limits<float>::quiet_NaN();
                require(scene.lighting().SetLightAnimInfo(light,invalid)==LT_INVALIDPARAMS,"nonfinite client light blend accepted");
                invalid=changed; invalid.m_iFrames[0]=frames;
                require(scene.lighting().SetLightAnimInfo(light,invalid)==LT_INVALIDPARAMS,"out-of-range client light frame accepted");
                require(scene.lighting().GetLightAnimInfo(light,actual)==LT_OK && actual.m_fPercentBetween==.375f && scene.lighting().SetLightAnimInfo(light,original)==LT_OK,"failed client light update changed state or restore failed"); ++light_animations;
            }
            std::vector<LTVector> authored_starts;
            for (const auto& definition : data->ObjectDefinitions()) if (definition.class_name=="GameStartPoint")
                for (const auto& property : definition.properties) if (property.name=="Pos" && property.type==PT_VECTOR && property.data.size()==12) {
                    LTVector start; for (uint32 axis=0;axis<3;++axis) start[axis]=nolf::wire::load<float>(property.data.data()+axis*4);
                    authored_starts.push_back(start); ++starts; start.y+=16;
                    auto end=start-LTVector{0,4096,0}; LTVector floor; LTPlane plane;
                    auto* hit=bsp_IntersectLine(geometry->GetRoot(),&start,&end,&floor,&plane);
                    if (!hit || plane.m_Normal.y<.5f || start.y-floor.y<8) continue;
                    LTVector dimensions{2,4,2},minimum=start-dimensions,maximum=start+dimensions;
                    if (ws_GetBoxStatus(geometry->GetRoot(),&minimum,&maximum)) continue;
                    for (uint32 stairs : {uint32(0),uint32(FLAG_STAIRSTEP)}) {
                        ObjectCreateStruct input; input.m_Pos=start; input.m_Flags=FLAG_SOLID|stairs; HOBJECT body=nullptr;
                        require(scene.create(input,body,nolf::ClientObjectLifetime::World)==LT_OK,"production client scene sweep object creation");
                        require(scene.physics().SetObjectDims(body,&dimensions,0)==LT_OK,"production client scene sweep dimensions");
                        auto destination=end; require(scene.physics().MoveObject(body,&destination,0)==LT_OK,"production client scene BSP sweep");
                        const auto result=body->m_Pos; const bool vertical=std::fabs(result.x-start.x)<.1f && std::fabs(result.z-start.z)<.1f;
                        const auto* contact=body->m_pStandingOnPoly ? body->m_pStandingOnPoly->GetPlane() : nullptr;
                        const auto clearance=contact ? contact->m_Normal.Dot(result)-contact->m_Dist : 0;
                        const auto support=contact ? phys_SupportRadius(dimensions,body->m_Flags,body->m_Flags2,contact->m_Normal) : 0;
                        auto core_min=body->m_BBoxMin+.2f,core_max=body->m_BBoxMax-.2f; bool overlap=false;
                        if (ws_GetBoxStatus(geometry->GetRoot(),&core_min,&core_max))
                            for (uint32 part=0;part<geometry->m_nParts && !overlap;++part) {
                                const auto* face=geometry->m_ppParts[part];
                                if (face->m_pParent->m_nFlags&WBSP_SOLID) overlap=w_PolygonTouchesBox(*face,core_min,core_max);
                            }
                        if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) || result.y>start.y+.5f || overlap ||
                            (vertical && result.y<floor.y-.15f) || (contact && (clearance<support-.15f || clearance>support+1)))
                            throw std::runtime_error("production native client scene BSP sweep mismatch: "+name);
                        if (contact) ++contacts;
                        require(scene.remove(body)==LT_OK,"production client scene sweep object removal"); ++sweeps;
                    }
                }
            CWorldBsp* render=nullptr;
            for (uint32 i=0;i<data->m_Worlds.GetSize();++i) if (data->m_Worlds[i]->m_pWorldBsp->m_nUnknown48&0x20) { render=data->m_Worlds[i]->m_pWorldBsp; break; }
            std::vector<HOBJECT> probes;
            for (const auto& start : authored_starts) { auto* probe=create(scene); require(scene.position(probe,start,true)==LT_OK,"visibility probe placement"); probes.push_back(probe); }
            if (render) {
                const auto graph=render->m_Visibility ? render->m_Visibility : WorldVisibility::Build(*render);
                std::vector<uint16> portals(render->m_nChunks); for (uint32 i=0;i<render->m_nChunks;++i) portals[i]=render->m_pChunks[i].m_Flags;
                for (const auto& start : authored_starts) {
                    const auto selection=graph->Select(start,portals); const auto visible=scene.visible(start,1e12f);
                    std::set<HOBJECT,std::less<HOBJECT>> actual; for (const auto& entry : visible) actual.insert(entry.object);
                    for (auto* probe : probes) {
                        const bool expected=graph->Intersects(selection,probe->m_Pos,probe->m_Radius);
                        require(actual.contains(probe)==expected,"client scene visibility did not use the render BSP's PVS");
                        if (!expected) ++visibility_rejections;
                    }
                }
            }
            for (auto* probe : probes) require(scene.remove(probe)==LT_OK,"visibility probe release");
            auto* network=create(scene); require(scene.server_id(network,1)==LT_OK,"world lifetime network identity setup");
            scene.client_flags(network,CF_NOTIFYREMOVE); nolf::ClientSceneEvents events;
            events.removed=[&](HOBJECT) { require(scene.server_id(camera,2)==LT_NOTINITIALIZED,"world unload callback converted persistent camera into a missed world object"); };
            scene.events(events);
            require(scene.unload_world()==LT_OK && !scene.main_world() && !scene.contains(world) && scene.contains(camera),"world unload did not retire level objects independently of UI camera"); ++worlds;
            require(!scene.contains(network) && !scene.find_server_object(1) && data->Geometry(),"world unload retained server identity or invalidated retained geometry");
            scene.events({});
        }
    }
    require(models && worlds && sweeps && visibility_rejections && light_animations && scene.size()==1,"original scene corpus was empty, unfiltered, unlit or leaked objects");
    std::cout << "Original native client scene: " << models << " ABC models, " << worlds << " worlds, " << starts << " starts, " << sweeps << " sweeps, " << contacts << " supporting polygons, " << visibility_rejections << " PVS rejections, " << light_animations << " light animations; persistent camera retained\n";
}
}
int main(int argc,char** argv) {
    nolf::platform::set_log_sink(diagnostic);
    struct ResetLog { ~ResetLog() { nolf::platform::set_log_sink(nullptr); } } reset;
    try { Files files; objects_and_cameras(); callbacks(); attachments(); models(files); node_callbacks(files); sdk_objects(files); if (argc==2) original_assets(argv[1]);
        require(!solver_errors,"production client scene movement emitted solver errors");
        std::cout << "Native client scene objects, cameras, SDK model/common interfaces, callbacks and retirement passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
