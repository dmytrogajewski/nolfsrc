#include "servermgr.h"
#include "servermsg.h"
#include "saveio.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/saved_game.h"
#include "engine/portable/engine_resources.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/wire.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include "platform/user_files.h"
#include "user_home_fixture.h"
#include "platform/module.h"
#include "object_lifetime.h"
#include "save_message.h"
#include "servermodel.h"
#include "worldsim.h"
#include "world_clip.h"
#include "physics_test_world.h"
#include "game/Shared/TriggerMessage.h"
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <random>
#include <span>

namespace {
std::function<void(BaseClass*,uint32,void*,float)> engine_hook;
std::function<void(uint32)> shell_phase_hook;
std::function<LTBOOL(HPOLY)> poly_filter;
std::function<LTBOOL(HOBJECT)> object_filter;
LTBOOL dispatch_poly_filter(HPOLY polygon, void*) { const auto callback = poly_filter; return callback ? callback(polygon) : LTTRUE; }
LTBOOL dispatch_object_filter(HOBJECT object, void*) { const auto callback = object_filter; return callback ? callback(object) : LTTRUE; }
void dispatch_shell_phase(uint32 phase) { const auto hook = shell_phase_hook; if (hook) hook(phase); }
void dispatch_hook(BaseClass* game,uint32 message,void* data,float parameter) { const auto hook = engine_hook; if (hook) hook(game,message,data,parameter); }
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
std::string console(CServerMgr& server,const char* name) {
    auto* value = reinterpret_cast<LTCommandVar*>(con_FindVar(&server.m_ConsoleState,name));
    return value && value->m_pStringVal ? value->m_pStringVal : "";
}
void actors(const char* module_path) {
    nolf::platform::Module observer(module_path);
    auto alive = reinterpret_cast<int (*)()>(observer.symbol("test_module_alive"));
    auto get_value = reinterpret_cast<uint32 (*)(BaseClass*)>(observer.symbol("test_module_value"));
    auto set_value = reinterpret_cast<void (*)(BaseClass*,uint32)>(observer.symbol("test_module_set_value"));
    auto get_trigger = reinterpret_cast<const char* (*)()>(observer.symbol("test_module_trigger"));
    auto set_hook = reinterpret_cast<void (*)(void (*)(BaseClass*,uint32,void*,float))>(observer.symbol("test_module_hook"));
    struct ResetHook { decltype(set_hook) set; ~ResetHook() { set(nullptr); engine_hook = {}; } } reset_hook{set_hook};
    set_hook(dispatch_hook);
    CServerMgr server;
    require(server.Init(),"live actor manager initializes");
    server.m_ClassMgr.configure(module_path);
    require(server.m_ClassMgr.load(server.m_pUnknown928) == LT_OK,"live actor manager loads native SDK module");
    auto* record = server.m_ClassMgr.find("NativeTestObject");
    require(record,"native game class registered");
    {
        CClassInfo foreign; foreign.m_pClassDef = record->m_pClassDef;
        auto* api = server.m_pUnknown928;
        const auto actual = reinterpret_cast<HCLASS>(record), invalid = reinterpret_cast<HCLASS>(&foreign);
        require(api->IsKindOf(actual,actual) && !api->IsKindOf(actual,invalid) && !api->IsKindOf(invalid,actual),
            "class predicates reject foreign registry records even when their definitions match");
        uint32 count = 99; char name[64] = "unchanged"; ClassPropInfo property{};
        require(api->GetNumClassProps(invalid,count) == LT_INVALIDPARAMS && !count &&
            api->GetClassName(invalid,name,sizeof(name)) == LT_INVALIDPARAMS && !name[0] &&
            api->GetClassProp(invalid,0,property) == LT_INVALIDPARAMS,
            "class reflection validates opaque handles before reading their fields");
        require(api->GetNumClassProps(actual,count) == LT_OK && count &&
            api->GetClassProp(actual,0,property) == LT_OK && property.m_PropName[0] &&
            api->GetClassProp(actual,count,property) == LT_NOTFOUND,
            "class reflection preserves inherited root-first properties and bounds");
    }
    LTVector force(1,-500,3), observed;
    require(server.m_pUnknown928->Physics()->SetGlobalForce(force) == LT_OK &&
        server.m_pUnknown928->Physics()->GetGlobalForce(observed) == LT_OK && observed.DistSqr(force) == 0 &&
        server.m_ForceControl.m_Force.DistSqr(force) == 0,"SDK global force uses the actual native server state");
    ObjectCreateStruct create;
    require(!server.CreateObject(reinterpret_cast<HCLASS>(record),&create),"creation requires a loaded world");
    server.m_bWorldLoaded = 1;
    {
        auto* api = server.m_pUnknown928;
        engine_hook = [&](BaseClass*,uint32 id,void*,float reason) {
            if (id != MID_PRECREATE) return;
            GenericProp value;
            if (reason == PRECREATE_NORMAL) {
                require(api->GetPropGeneric("Tag",&value) == LT_NOTFOUND,"normal nested creation has its own empty property scope");
                return;
            }
            require(reason == PRECREATE_STRINGPROP && api->GetPropGeneric("Tag",&value) == LT_OK,"property creation supplies the SDK precreate reason and scope");
            if (std::string_view(value.m_String) == "inner") return;
            require(std::string_view(value.m_String) == "outer" && api->GetPropGeneric("Number",&value) == LT_OK && value.m_Long == 123,
                    "property strings provide SDK numeric conversions");
            ObjectCreateStruct nested;
            auto* inner = api->CreateObjectProps(reinterpret_cast<HCLASS>(record),&nested,const_cast<char*>("Tag inner;"));
            require(inner && api->GetPropGeneric("Tag",&value) == LT_OK && std::string_view(value.m_String) == "outer",
                    "nested property creation restores the outer owned values");
            sm_RemoveObjectFromWorld(&server,inner);
            auto* normal = api->CreateObject(reinterpret_cast<HCLASS>(record),&nested);
            require(normal && api->GetPropGeneric("Tag",&value) == LT_OK && std::string_view(value.m_String) == "outer",
                    "normal nested creation restores the outer property scope");
            sm_RemoveObjectFromWorld(&server,normal);
            char tiny[2] = {'x','z'};
            require(api->GetPropString("Tag",tiny,1) == LT_OK && tiny[0] == 0 && tiny[1] == 'z' &&
                    api->GetPropString("Tag",tiny,0) == LT_INVALIDPARAMS,"property string output is terminated within its declared capacity");
        };
        ObjectCreateStruct properties_create;
        auto* property_game = api->CreateObjectProps(reinterpret_cast<HCLASS>(record),&properties_create,const_cast<char*>("Tag outer; Number 123;"));
        require(property_game,"property-created fixture actor attaches to native world");
        engine_hook = {};
        GenericProp value;
        require(api->GetPropGeneric("Tag",&value) == LT_NOTFOUND && !server.m_Properties,"creation releases the complete property context");
        sm_RemoveObjectFromWorld(&server,property_game);
        require(!api->CreateObjectProps(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("RefusingTestObject")),&properties_create,const_cast<char*>("Tag refused;")) &&
                !server.m_Properties && alive() == 0,"precreate refusal releases property context and game instance");
    }
    require(!server.CreateObject(reinterpret_cast<HCLASS>(std::uintptr_t(1)),&create),"foreign class handles are rejected without dereference");
    require(!server.CreateObject(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("RefusingTestObject")),&create) && server.m_Unknown390.m_Allocated.empty(),"precreate refusal never publishes an actor");
    require(!server.CreateObject(reinterpret_cast<HCLASS>(server.m_ClassMgr.find("ThrowingTestObject")),&create),"throwing native constructors do not publish actors");
    std::snprintf(create.m_Name,sizeof(create.m_Name),"SaveActor");
    create.m_Pos.Init(10,20,30);
    auto* game = server.CreateObject(reinterpret_cast<HCLASS>(record),&create);
    require(game && alive() == 1 && game->m_hObject->m_pSData->m_pBaseClass == game,"real actor creation publishes the actual game class");
    auto* object = game->m_hObject;
    auto* api = server.m_pUnknown928;
    uint32 portal_flags=123;
    char missing_portal[]="UncompiledPortal";
    require(api->GetPortalFlags(missing_portal,&portal_flags)==LT_NOTFOUND && !portal_flags,
        "absent optional visibility portal retained undefined flags");
    auto* absent=api->StartHMessageWrite();
    require(absent && api->WriteToMessageHMessageRead(absent,nullptr)==LT_OK,"optional nested message serialization failed");
    absent->ResetPos();
    uint32 absent_status=0;
    require(absent->ReadWord()==0xffff && absent->GetStatus(absent_status)==LT_OK && !absent_status,"optional nested message lost its null sentinel");
    absent->Release();
    require(api->HandleToObject(object)==game && !api->HandleToObject(nullptr) &&
        !api->HandleToObject(reinterpret_cast<HOBJECT>(std::uintptr_t(1))),"SDK handle lookup must validate object ownership before dereferencing it");
    auto send_trigger = [&](const char* text) {
        auto* message = api->StartMessageToObject(game, object, 1000);
        require(message && WriteTriggerMessage(message, text) == LT_OK, "owned trigger encodes");
        api->EndMessage(message);
    };
    std::string trigger(700, 'x'); trigger += " final trigger";
    const auto expected_trigger = trigger;
    auto* trigger_message = api->StartMessageToObject(game, object, 1000);
    require(trigger_message && WriteTriggerMessage(trigger_message, trigger.c_str()) == LT_OK, "trigger copies source text");
    trigger.assign(trigger.size(), 'y');
    engine_hook = [&](BaseClass*,uint32 id,void* text,float) {
        if (id != 1000 || std::string_view(static_cast<char*>(text)) == "nested trigger") return;
        const auto outer = std::string(static_cast<char*>(text));
        send_trigger("nested trigger");
        require(std::string(static_cast<char*>(text)) == outer, "nested trigger keeps outer receiver storage alive");
    };
    api->EndMessage(trigger_message);
    engine_hook = {};
    require(std::string(get_trigger()) == expected_trigger, "trigger dispatch survives source replacement and nested callbacks");
    object->SetDims({2,3,4}); object->m_Velocity.Init(1,2,3); object->m_Mass = 41;
    set_value(game,0x12345678);
    ObjectLink link{object,nullptr}; ObjectList list{&link,1};
    nolf::MemoryStream output({},true);
    require(sav_SaveObjects(&server,&output,&list,93,0) == LT_OK,"real actor save crosses SDK module callback");
    const auto document = nolf::DecodeSavedGame(output.data());
    require(document.objects.size() == 1 && document.objects[0].name == "SaveActor" && document.objects[0].position == nolf::SavedVector{10,20,30},"real actor engine fields captured");
    ObjectCallbackGuard original(*object);
    require(sm_RemoveObjectFromWorld(&server,game) == LT_OK && !original.alive() && alive() == 0 && !server.m_pChangedObjects,"real actor removal releases both game and native identities");
    require(!api->HandleToObject(object),"SDK handle lookup dereferenced a retired cinematic speaker");
    require(output.SeekTo(0) == LT_OK && sav_RestoreObjects(&server,&output,93,0) == LT_OK,"real actor restore crosses native creation and physics");
    require(server.m_Unknown390.m_Allocated.size() == 1 && alive() == 1,"real actor restore owns one native and game object");
    auto* restored = server.m_Unknown390.m_Allocated.begin()->first;
    require(restored->m_Pos.DistSqr({10,20,30}) == 0 && restored->m_Dims.DistSqr({2,3,4}) == 0 && restored->m_Mass == 41 && get_value(restored->m_pSData->m_pBaseClass) == 0x12345678,"restore preserves engine state and game payload");
    nolf::MemoryStream again({},true); link.m_hObject = restored;
    require(sav_SaveObjects(&server,&again,&list,93,0) == LT_OK,"restored actor saves again");
    const auto second = nolf::DecodeSavedGame(again.data());
    require(second.objects == document.objects,"actual actor save restore save preserves original engine and game wire records");
    for (std::size_t i = 0; i < 6; ++i) require(second.timing_words[i] == document.timing_words[i],"restore preserves simulation timing while rebasing the platform clock");
    {
        // Save names are the game's logical paths, resolved under the user data
        // root like the menus' slot checks; host paths and traversal are refused.
        const ScopedUserHome home; const auto root = home.data();
        require(sav_SaveFile(&server,"Save\\Slot07.sav",&list,93,0) == LT_OK && std::filesystem::is_regular_file(root/"save"/"slot07.sav"),"game save name not resolved under the user data root");
        require(!std::filesystem::exists("Save\\Slot07.sav"),"game save name written as a host file name");
        require(nolf::platform::UserFiles{}.file_exists("save/SLOT07.SAV"),"saved slot invisible to the menus' existence check");
        const auto outside = (root/"escape.sav").string();
        require(sav_SaveFile(&server,outside.c_str(),&list,93,0) != LT_OK && sav_SaveFile(&server,"Save\\..\\..\\escape.sav",&list,93,0) != LT_OK && !std::filesystem::exists(outside),"host or traversing save path accepted");
        require(sav_RestoreFile(&server,"SAVE/slot07.sav",93,0) == LT_OK && alive() == 2,"saved slot does not restore by its game name");
        for (const auto& [object,owner] : server.m_Unknown390.m_Allocated) if (object != restored) { require(sm_RemoveObjectFromWorld(&server,object->m_pSData->m_pBaseClass) == LT_OK && alive() == 1,"file-restored actor removal"); break; }
        require(sav_RestoreFile(&server,outside.c_str(),93,0) != LT_OK,"host restore path accepted");
    }
    require(sm_RemoveObjectFromWorld(&server,restored->m_pSData->m_pBaseClass) == LT_OK,"remove restored actor");
    {
        auto pending = document;
        pending.objects[0].change_flags |= 1;
        const auto ids = server.m_Unknown8bc;
        for (unsigned round=0;round<3;++round) {
            nolf::MemoryStream saved_pending(nolf::EncodeSavedGame(pending));
            require(sav_RestoreObjects(&server,&saved_pending,93,0) == LT_OK,"restore an actor queued for deletion by a save callback");
            require(!server.m_Link83c.IsTiedOff(),"restored pending deletion has an actual queue entry");
            ObjectCallbackGuard queued(*server.m_Unknown390.m_Allocated.begin()->first);
            sm_DrainObjectQueue(&server);
            require(!queued.alive() && alive() == 0 && server.m_Unknown390.m_Allocated.empty() && server.m_Link83c.IsTiedOff(),
                "restored pending deletion retires native and game identities");
            require(server.m_Unknown8bc == ids,"repeated restore reuses deleted object IDs");
        }
    }
    for (auto message : {MID_ACTIVATING,MID_INITIALUPDATE}) for (bool drain : {false,true}) {
        engine_hook = [&](BaseClass* self,uint32 current,void*,float) {
            if (current != uint32(message)) return;
            require(self->m_hObject && self->m_hObject->m_pSData->m_pBaseClass == self && sm_FindObjectByID(&server,self->m_hObject->m_ObjectID) == self->m_hObject,
                "activation sees a complete native identity and game class");
            sm_RemoveObject(&server,self->m_hObject);
            if (drain) sm_DrainObjectQueue(&server);
        };
        require(!server.CreateObject(reinterpret_cast<HCLASS>(record),&create) && alive() == 0 && server.m_Unknown390.m_Allocated.empty() && server.m_Link83c.IsTiedOff() && !server.m_pChangedObjects,
            "self removal during creation preserves queue, class ownership and native identity");
    }
    engine_hook = {};
    auto make = [&](const char* name,LTVector position,uint32 flags) {
        ObjectCreateStruct request; request.m_Pos = position; request.m_Flags = flags;
        std::snprintf(request.m_Name,sizeof(request.m_Name),"%s",name);
        auto* created = server.CreateObject(reinterpret_cast<HCLASS>(record),&request);
        require(created,"physics actor creation");
        LTVector dimensions(2,2,2);
        require(server.m_pUnknown928->Physics()->SetObjectDims(created->m_hObject,&dimensions,0) == LT_OK,"physics actor dimensions and spatial membership");
        created->m_hObject->m_ForceIgnoreLimitSqr = 0;
        return created->m_hObject;
    };
    auto* first = make("SolidA",{0,0,0},FLAG_SOLID | FLAG_TOUCH_NOTIFY);
    auto* other = make("SolidB",{10,0,0},FLAG_SOLID | FLAG_TOUCH_NOTIFY);
    require(server.m_pUnknown928->Physics()->SetObjectDims(first,nullptr,0) == LT_INVALIDPARAMS &&
        server.m_pUnknown928->Physics()->SetAcceleration(first,nullptr) == LT_INVALIDPARAMS &&
        server.m_pUnknown928->Physics()->MoveObject(first,nullptr,0) == LT_INVALIDPARAMS,"native physics validates borrowed vector arguments before reading them");
    int touches = 0;
    engine_hook = [&](BaseClass*,uint32 message,void*,float) { if (message == MID_TOUCHNOTIFY) ++touches; };
    LTVector destination(9,0,0);
    require(server.m_pUnknown928->Physics()->MoveObject(first,&destination,0) == LT_OK && first->m_BBoxMax.x <= other->m_BBoxMin.x + .11f && touches > 0 && !server.m_pCollisionInfo,
        "actual native box collision resolves overlap and dispatches SDK touch callbacks");
    engine_hook = {};
    auto* parent = make("Parent",{0,50,0},0);
    auto* child = make("Child",{2,50,0},0);
    LTVector offset(2,0,0); LTRotation rotation; rotation.Init();
    require(am_CreateAttachment(&server.m_Unknown390,parent,child,-1,&offset,&rotation,nullptr) == LT_OK,"live actors attach through native indexed ownership");
    destination.Init(15,50,0);
    sm_MoveObject(&server,parent,&destination,0xf);
    require(child->m_Pos.DistSqr({17,50,0}) == 0,"native mover follows live attached child");
    server.m_SkyObjects[3] = parent->m_ObjectID; parent->m_ChangeFlags |= 0x100;
    require(server.m_ClientReferences.Add(*child,73,"Restored player"),"live actor client reference");
    parent->m_pSData->m_SFXMessage.Assign(new ServerMessage);
    parent->m_pSData->m_SFXMessage.m_pMsg->WriteByte(19);
    ObjectLink child_link{child,nullptr}, parent_link{parent,&child_link}; ObjectList graph{&parent_link,2};
    engine_hook = [&](BaseClass* self,uint32 message,void*,float) {
        if (message != MID_SAVEOBJECT || self->m_hObject != parent) return;
        parent->m_Mass = 97;
        child->m_Mass = 73; set_value(child->m_pSData->m_pBaseClass,88);
        require(server.m_InterObjectLinks.Create(parent,child,0),"save callback can create native graph relations");
    };
    nolf::MemoryStream graph_output({},true);
    require(sav_SaveObjects(&server,&graph_output,&graph,4,0) == LT_OK,"live actor graph saves after callback mutation");
    engine_hook = {};
    const auto graph_document = nolf::DecodeSavedGame(graph_output.data());
    require(graph_document.objects[0].mass == 30 && graph_document.objects[1].mass == 73 &&
        graph_document.objects[0].relations.size() == 1 && graph_document.objects[0].attachments.size() == 1 &&
        graph_document.objects[0].sky_slot == 3 && graph_document.objects[1].client && graph_document.objects[0].sfx_message,
        "save snapshots engine state before each actor callback and graph after all callbacks");
    require(sm_RemoveObjectFromWorld(&server,parent->m_pSData->m_pBaseClass) == LT_OK &&
        sm_RemoveObjectFromWorld(&server,child->m_pSData->m_pBaseClass) == LT_OK,"remove original saved graph");
    const auto before_actors = server.m_Unknown390.m_Allocated.size();
    auto damaged = graph_document; damaged.objects[1].game_data.pop_back();
    nolf::MemoryStream bad_game(nolf::EncodeSavedGame(damaged));
    require(sav_RestoreObjects(&server,&bad_game,4,0) != LT_OK && server.m_Unknown390.m_Allocated.size() == before_actors && alive() == int(before_actors) && !server.m_Restoring,
        "failed live game-payload restore rolls back all new actors and retains preexisting actors");
    require(graph_output.SeekTo(0) == LT_OK && sav_RestoreObjects(&server,&graph_output,4,0) == LT_OK,"live graph restores through real actor SDK callbacks");
    auto find_actor = [&](const char* name) {
        for (const auto& [actor,type] : server.m_Unknown390.m_Allocated)
            if (actor->m_pSData->m_hIdentifier && std::string(dhash_GetKey(actor->m_pSData->m_hIdentifier,0)) == name) return actor;
        return static_cast<LTObject*>(nullptr);
    };
    parent = find_actor("Parent"); child = find_actor("Child");
    require(parent && child,"restored graph has both named actors");
    require(parent->m_pAttachments && parent->m_pAttachments->m_pChild == child,"restored graph attachment binds native child");
    require(server.m_SkyObjects[3] == parent->m_ObjectID,"restored graph sky binds native identity");
    require(get_value(child->m_pSData->m_pBaseClass) == 88,"restored graph game payload observes prior actor save callback");
    require(parent->m_pSData->m_SFXMessage.m_pMsg && parent->m_pSData->m_SFXMessage.m_pMsg->m_DataLen == graph_document.objects[0].sfx_message->size(),"restored graph SFX bytes retained");
    require(server.m_ClientReferences.Find(server.m_ClientReferences.Next(nullptr)) && server.m_ClientReferences.Find(server.m_ClientReferences.Next(nullptr))->object == child,"restored graph pending client binds native identity");
    graph_output = nolf::MemoryStream({},true);
    parent_link.m_hObject = parent; child_link.m_hObject = child;
    require(sav_SaveObjects(&server,&graph_output,&graph,4,0) == LT_OK && nolf::DecodeSavedGame(graph_output.data()).objects == graph_document.objects,
        "live native attachment, inter-object and client graph round trips without original object IDs");
    auto* platform = make("StandingPlatform",{0,500,0},FLAG_SOLID);
    std::vector<LTObject*> riders;
    for (int i = 0; i != 80; ++i) {
        auto* rider = make("Rider",{float(i)*10,505,0},0);
        obj_SetStandingOn(rider,platform,nullptr); riders.push_back(rider);
    }
    destination.Init(5,500,0); sm_MoveObject(&server,platform,&destination,7);
    for (std::size_t i = 0; i < riders.size(); ++i) require(riders[i]->m_Pos.x == float(i)*10 + 5,"standing movement carries every rider beyond the original 32-record stack limit");
    auto* sensor = make("LargeSensor",{0,800,0},FLAG_TOUCH_NOTIFY);
    std::vector<LTObject*> sensed;
    for (int i = 0; i != 180; ++i) sensed.push_back(make("Sensed",{10,800,0},FLAG_TOUCH_NOTIFY));
    int sensor_touches = 0;
    engine_hook = [&](BaseClass* self,uint32 message,void*,float) { if (message == MID_TOUCHNOTIFY && self->m_hObject == sensor) ++sensor_touches; };
    destination.Init(10,800,0); sm_MoveObject(&server,sensor,&destination,7); engine_hook = {};
    require(sensor_touches == 180,"native swept query retains every contact beyond the original 128-object buffer");
    for (bool split : {false,true}) for (bool stairs : {false,true}) {
        TestPhysicsWorld geometry(split);
        auto* actor = make("FallRegression",{0,80,0},FLAG_SOLID);
        actor->SetDims({2,4,2}); actor->m_Velocity.Init(0,-100,0);
        auto box_minimum = actor->m_BBoxMin, box_maximum = actor->m_BBoxMax;
        require(ws_GetBoxStatus(geometry.GetRoot(),&box_minimum,&box_maximum) &&
            !ws_BoxTouchesSolidSurface(geometry.GetRoot(),box_minimum,box_maximum),
            "main-world surface contact remains distinct from brush and container volume classification");
        CollisionInfo collision{};
        CMoveState request{};
        request.m_pHost = server.m_pUnknown6a8; request.m_pInfo = &collision;
        request.m_pWorldBsp = &geometry; request.m_pObject = actor;
        request.m_vStart = actor->m_Pos; request.m_vEnd.Init(0,-1000,0);
        request.m_vDims = actor->m_Dims; request.m_n38 = stairs; request.m_bPushOut = 1;
        CBounceState result;
        if (split) {
            geometry.expose_polygons = false;
            auto* before_state = g_pMoveState; auto* before_result = g_pMoveResult; auto* before_block = g_pPBlock;
            bool rejected = false;
            try { ws_MoveObject(&request,&result); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected && g_pMoveState == before_state && g_pMoveResult == before_result && g_pPBlock == before_block,
                "failed physics broadphase restores independent scratch and stack request ownership");
            geometry.expose_polygons = true;
        }
        ws_MoveObject(&request,&result);
        const float floor = split ? -300.f : 0.f;
        require(result.m_nHits && result.m_pStandingPoly == geometry.GetRoot() && result.m_vPoint.y >= floor+3.85f && result.m_vPoint.y <= floor+4.25f,
            "native long fall stops on its floor across cell boundaries and never creates a negative stair foot volume");
        geometry.ClearPhysics();
        ws_MoveObject(&request,&result);
        require(result.m_nHits == 0 && !result.m_pStandingPoly && result.m_vPoint.DistSqr(request.m_vStart) == 0,"missing physics blocks return a defined unchanged position");
        require(sm_RemoveObjectFromWorld(&server,actor->m_pSData->m_pBaseClass) == LT_OK,"native fall regression actor removes cleanly");
    }
    for (int action = 0; action != 4; ++action) {
        if (action == 3) server.m_bWorldLoaded = 1;
        auto* moving_actor = make("CallbackMover",{0,100 + float(action)*20,0},FLAG_SOLID | FLAG_TOUCH_NOTIFY);
        auto* blocker = make("CallbackBlocker",{10,100 + float(action)*20,0},FLAG_SOLID | FLAG_TOUCH_NOTIFY);
        ObjectCallbackGuard mover_before(*moving_actor), blocker_before(*blocker);
        bool called = false;
        engine_hook = [&](BaseClass*,uint32 message,void*,float) {
            if (message != MID_TOUCHNOTIFY || called) return;
            called = true;
            if (action == 0) sm_RemoveObjectFromWorld(&server,blocker->m_pSData->m_pBaseClass);
            else if (action == 1) sm_RemoveObjectFromWorld(&server,moving_actor->m_pSData->m_pBaseClass);
            else if (action == 2) server.EndWorld(0);
            else server.Term();
        };
        destination.Init(9,100 + float(action)*20,0);
        require(server.m_pUnknown928->Physics()->MoveObject(moving_actor,&destination,0) == LT_OK && called && !server.m_pCollisionInfo,
            "collision callbacks can remove either endpoint or end the world without stale physics requests");
        engine_hook = {};
        if (action == 0) require(mover_before.unchanged() && !blocker_before.alive() && !(moving_actor->m_ChangeFlags & 2),"removed blocker is not reused after touch callback");
        if (action == 1) require(!mover_before.alive() && blocker_before.unchanged(),"removed mover is not reused after touch callback");
        if (action == 2) require(!mover_before.alive() && !blocker_before.alive() && server.m_Unknown390.m_Allocated.empty(),"world-end callback retires all actor generations");
        if (action == 3) require(!mover_before.alive() && !blocker_before.alive() && !server.m_pUnknown6a8 && !server.m_Initialized,"teardown callback destroys the mover while native physics unwinds safely");
    }
    server.Term();
    require(alive() == 0 && server.m_Unknown390.m_Allocated.empty(),"server teardown removes live SDK actors");
}
// m01s04's opening hill. Terrain sections share the polygons on their
// borders, so one slope reaches a move through several world models, and the
// push loop retries each of them: a lift or a shove that is not measured from
// the destination is applied again by every one of them.
void terrain_walk(CServerMgr& server,HCLASS klass) {
    std::size_t sections = 0;
    for (auto* record : std::span(server.m_World.m_Worlds.GetArray(),server.m_World.m_Worlds.GetSize())) {
        if (!(record->m_pWorldBsp->m_nUnknown48 & 8)) continue;
        for (uint32 section = 0; section < record->m_pWorldBsp->m_Array138.GetSize(); ++section) {
            ObjectCreateStruct create; create.m_ObjectType = OT_WORLDMODEL; create.m_Flags = FLAG_SOLID | FLAG_RAYHIT;
            std::snprintf(create.m_Filename,sizeof(create.m_Filename),"#$#%u %s",unsigned(section),record->m_pWorldBsp->m_Name);
            require(server.CreateObject(klass,&create),"original terrain section becomes a world model");
            ++sections;
        }
    }
    require(sections > 1,"original level has several terrain sections");
    ObjectCreateStruct create; create.m_Pos.Init(-13484.2f,564.8f,-23449.2f);
    create.m_Flags = FLAG_SOLID | FLAG_STAIRSTEP; create.m_Flags2 = FLAG2_PLAYERCOLLIDE;
    auto* game = server.CreateObject(klass,&create);
    require(game,"terrain walker is created");
    auto* walker = game->m_hObject;
    LTVector dimensions(24,53,24);
    require(server.m_pUnknown928->Physics()->SetObjectDims(walker,&dimensions,0) == LT_OK,"terrain walker has a player's dimensions");
    // Up the 36-degree slope across the section border and back down it. The
    // fall per frame outruns the slope, so a walker that is not lifted onto it
    // or put down on it shows up as a frame without a floor. Where the slope
    // eases off, the cylinder rests on the crease between two faces for one
    // frame, which is not a lost floor.
    for (float direction : {1.f,-1.f}) {
        int airborne = 0;
        for (int frame = 0; frame != 55; ++frame) {
            const auto before = walker->m_Pos;
            auto destination = before + LTVector(0,-6,5*direction);
            require(server.m_pUnknown928->Physics()->MoveObject(walker,&destination,0) == LT_OK,"terrain walker moves");
            const float rise = walker->m_Pos.y-before.y;
            airborne = walker->m_pStandingOnPoly ? 0 : airborne+1;
            if (airborne > 1 || rise > 4.5f || rise < -4.5f || std::fabs(walker->m_Pos.z-before.z) < 4.9f) {
                std::cerr << "terrain walk frame " << frame << " direction " << direction << " from " << before.x << ',' << before.y << ',' << before.z
                    << " to " << walker->m_Pos.x << ',' << walker->m_Pos.y << ',' << walker->m_Pos.z << " standing " << bool(walker->m_pStandingOnPoly) << '\n';
                throw std::runtime_error("terrain walker left the slope across shared section polygons");
            }
        }
        require(walker->m_pStandingOnPoly,"terrain walker ends each walk standing");
    }
    require(walker->m_Pos.DistSqr(create.m_Pos) < 1,"terrain walker retraces the slope back to where it started");
    require(sm_RemoveObjectFromWorld(&server,game) == LT_OK,"terrain walker removes cleanly");
}
void original_worlds(const char* module_path,const std::filesystem::path& data_path) {
    nolf::Assets assets; assets.mount_game(data_path);
    CServerMgr server; require(server.Init(),"original-world physics server initializes");
    server.m_ClassMgr.configure(module_path);
    require(server.m_ClassMgr.load(server.m_pUnknown928) == LT_OK,"original-world fixture game module loads");
    auto klass = reinterpret_cast<HCLASS>(server.m_ClassMgr.find("NativeTestObject"));
    std::size_t worlds = 0, starts = 0, sweeps = 0, grounded = 0, world_hits = 0;
    for (auto name : assets.names()) {
        if (!name.starts_with("worlds/") || !name.ends_with(".dat")) continue;
        nolf::AssetStream stream(assets.locate(name));
        require(server.LoadWorld(&stream,name.data()) == LT_OK,"actual server loads original native level geometry");
        auto* geometry = server.m_World.FindFlagged10();
        require(geometry,"original level has main physics geometry");
        ObjectCreateStruct world_create; world_create.m_ObjectType = OT_WORLDMODEL; world_create.m_Flags = FLAG_SOLID;
        std::snprintf(world_create.m_Filename,sizeof(world_create.m_Filename),"%s",geometry->m_Name);
        auto* world_game = server.CreateObject(klass,&world_create);
        require(world_game && world_game->m_hObject->m_pWorldBsp == geometry,"actual server creates the original physics world model");
        std::size_t level_hits = 0;
        for (const auto& definition : server.m_World.ObjectDefinitions()) {
            if (definition.class_name != "GameStartPoint") continue;
            for (const auto& property : definition.properties) {
                if (property.name != "Pos" || property.type != PT_VECTOR || property.data.size() != 12) continue;
                ++starts;
                LTVector start;
                for (uint32 axis = 0; axis < 3; ++axis) start[axis] = nolf::wire::load<float>(property.data.data()+axis*4);
                start.y += 16;
                auto end = start; end.y -= 4096;
                LTVector floor; LTPlane plane;
                auto* floor_polygon = bsp_IntersectLine(geometry->GetRoot(),&start,&end,&floor,&plane);
                if (!floor_polygon || plane.m_Normal.y < .5f || start.y-floor.y < 8) continue;
                LTVector dimensions(2,4,2), minimum = start-dimensions, maximum = start+dimensions;
                if (ws_GetBoxStatus(geometry->GetRoot(),&minimum,&maximum)) continue;
                for (uint32 stair : {uint32(0),uint32(FLAG_STAIRSTEP)}) {
                    ObjectCreateStruct create; create.m_Pos = start; create.m_Flags = FLAG_SOLID | stair;
                    auto* game = server.CreateObject(klass,&create);
                    require(game,"actual server creates original-level sweep actor");
                    auto* object = game->m_hObject;
                    require(server.m_pUnknown928->Physics()->SetObjectDims(object,&dimensions,0) == LT_OK,"original-level actor dimensions");
                    auto destination = end;
                    require(server.m_pUnknown928->Physics()->MoveObject(object,&destination,0) == LT_OK,"native physics sweeps against original level BSP");
                    const auto* standing = object->m_pStandingOnPoly ? object->m_pStandingOnPoly->GetPlane() : nullptr;
                    const float clearance = standing ? standing->m_Normal.Dot(object->m_Pos)-standing->m_Dist : -1;
                    const float support = standing ? std::fabs(standing->m_Normal.x)*dimensions.x + std::fabs(standing->m_Normal.y)*dimensions.y + std::fabs(standing->m_Normal.z)*dimensions.z : 0;
                    LTVector body_minimum = object->m_BBoxMin+.2f, body_maximum = object->m_BBoxMax-.2f;
                    bool penetrating = false;
                    if (ws_GetBoxStatus(geometry->GetRoot(),&body_minimum,&body_maximum)) {
                        // BSP leaves also partition nonblocking surfaces. A
                        // coarse box answer needs finite solid-face checks.
                        for (uint32 p = 0; p < geometry->m_nParts && !penetrating; ++p) {
                            const auto* part = geometry->m_ppParts[p];
                            if (!(part->m_pParent->m_nFlags & WBSP_SOLID) || !part->m_nVertices) continue;
                            auto face_minimum = *part->m_Vertices[0].m_pPos, face_maximum = face_minimum;
                            for (uint32 v = 1; v < part->m_nVertices; ++v) for (uint32 axis = 0; axis < 3; ++axis) {
                                face_minimum[axis] = std::min(face_minimum[axis],(*part->m_Vertices[v].m_pPos)[axis]);
                                face_maximum[axis] = std::max(face_maximum[axis],(*part->m_Vertices[v].m_pPos)[axis]);
                            }
                            if (face_minimum.x > body_maximum.x || face_minimum.y > body_maximum.y || face_minimum.z > body_maximum.z ||
                                face_maximum.x < body_minimum.x || face_maximum.y < body_minimum.y || face_maximum.z < body_minimum.z) continue;
                            penetrating = w_PolygonTouchesBox(*part,body_minimum,body_maximum);
                        }
                    }
                    const bool vertical = std::fabs(object->m_Pos.x-start.x) < .1f && std::fabs(object->m_Pos.z-start.z) < .1f;
                    if (!std::isfinite(object->m_Pos.x) || !std::isfinite(object->m_Pos.y) || !std::isfinite(object->m_Pos.z) ||
                        object->m_Pos.y > start.y + .5f || penetrating || (vertical && object->m_Pos.y < floor.y-.15f) ||
                        (standing && (clearance < support-.15f || clearance > support+1))) {
                        std::cerr << "BSP sweep mismatch " << name << " model " << geometry->m_Name << " flags " << geometry->GetWorldFlags()
                            << " start " << start.x << ',' << start.y << ',' << start.z << " floor " << floor.y
                            << " result " << object->m_Pos.x << ',' << object->m_Pos.y << ',' << object->m_Pos.z << " standing " << bool(standing) << " clearance " << clearance << " support " << support << " penetrating " << penetrating << " stair " << stair << '\n';
                        const auto& grid = geometry->m_Sub108;
                        auto* block = geometry->GetPointStatus(start); auto* floor_block = geometry->GetPointStatus(floor);
                        std::cerr << "grid " << grid.m_nCountX << ',' << grid.m_nCountY << ',' << grid.m_nCountZ << " cell " << grid.m_vCell.x << ',' << grid.m_vCell.y << ',' << grid.m_vCell.z
                            << " start block " << (block-grid.m_pRecords) << " nodes " << block->m_nCount << " floor block " << (floor_block-grid.m_pRecords) << " nodes " << floor_block->m_nCount << '\n';
                        std::cerr << "center free " << bsp_PointInside(geometry->GetRoot(),&object->m_Pos) << " bounds " << body_minimum.x << ',' << body_minimum.y << ',' << body_minimum.z << " to " << body_maximum.x << ',' << body_maximum.y << ',' << body_maximum.z << '\n';
                        int faces = 0;
                        for (uint32 p = 0; p < geometry->m_nParts; ++p) {
                            const auto* part = geometry->m_ppParts[p];
                            if (!(part->m_pParent->m_nFlags & WBSP_SOLID) || !w_PolygonTouchesBox(*part,body_minimum,body_maximum)) continue;
                            const auto* face = part->m_pParent->m_pPlane;
                            if (faces++ < 5) std::cerr << "overlap poly " << p << " normal " << face->m_Normal.x << ',' << face->m_Normal.y << ',' << face->m_Normal.z << " dist " << face->m_Dist << '\n';
                        }
                        std::cerr << "solid face overlaps " << faces << '\n';
                        throw std::runtime_error("native sweep did not stop on original level floor");
                    }
                    require(!server.m_pCollisionInfo && !(object->m_ChangeFlags & 2),"original-level movement restores request state");
                    require(sm_RemoveObjectFromWorld(&server,game) == LT_OK,"original-level sweep actor removes cleanly");
                    ++sweeps; ++level_hits; grounded += standing != nullptr;
                }
            }
        }
        world_hits += level_hits != 0;
        if (name == "worlds/m01s04.dat") terrain_walk(server,klass);
        server.EndWorld(0); ++worlds;
        require(server.m_Unknown390.m_Allocated.empty() && !server.m_pChangedObjects && !g_WorldBytes,"original-level server releases actors and native geometry between worlds");
    }
    require(worlds && sweeps,"original-world physics checks exercised authored game starts");
    std::cout << "Original BSP physics: " << sweeps << " sweeps, " << grounded << " supporting contacts at " << starts << " authored starts across " << world_hits << '/' << worlds << " levels\n";
}
void segment_queries() {
    CUnknown210 tree;
    auto geometry = std::make_unique<TestPhysicsWorld>(false);
    auto world = std::make_unique<WorldModelObject>();
    world->m_pWorldBsp = geometry.get(); world->m_Flags = FLAG_SOLID;
    world->m_Transform1f0.Identity(); world->m_Transform230.Identity();
    world->m_Transform1f0.m[1][3] = 10; world->m_Transform230.m[1][3] = -10;
    world->m_BBoxMin.Init(-1000,-1024,-1000); world->m_BBoxMax.Init(1000,1024,1000);
    geometry->FindPoly(1u<<16)->m_pSurface->m_nTextureFlags = 0x1234;
    tree.BumpObject(world.get(),0);
    IntersectQuery query; query.m_From.Init(0,50,0); query.m_To.Init(0,-50,0);
    query.m_Flags = INTERSECT_OBJECTS|IGNORE_NONSOLID|INTERSECT_HPOLY;
    IntersectInfo info;
    require(w_IntersectSegment(&query,&info,&tree,0) && info.m_hObject == world.get() && info.m_hPoly == (1u<<16) &&
            std::fabs(info.m_Point.y-10) < .001f && std::fabs(info.m_Plane.m_Dist-10) < .001f && info.m_SurfaceFlags == 0x1234,
            "segment geometry uses native polygon records and transforms point, plane, handle and surface flags");
    query.m_PolyFilterFn = dispatch_poly_filter;
    poly_filter = [&](HPOLY) {
        auto nested = query; nested.m_PolyFilterFn = nullptr;
        IntersectInfo result;
        require(w_IntersectSegment(&nested,&result,&tree,0) && result.m_Point.y == 10,"nested segment query owns an independent cursor");
        return LTTRUE;
    };
    require(w_IntersectSegment(&query,&info,&tree,0) && info.m_Point.y == 10,"polygon filter preserves outer query after nested dispatch");
    poly_filter = [&](HPOLY) { world->m_Transform1f0.m[1][3] = 30; return LTTRUE; };
    require(!w_IntersectSegment(&query,&info,&tree,0),"moving a queried world invalidates its previous transform snapshot");
    world->m_Transform1f0.m[1][3] = 10;
    poly_filter = [&](HPOLY) { geometry.reset(); return LTTRUE; };
    require(!w_IntersectSegment(&query,&info,&tree,0),"polygon filter can destroy native geometry before the traversal resumes");
    poly_filter = {}; query.m_PolyFilterFn = nullptr; world.reset();
    auto near = std::make_unique<LTObject>(), far = std::make_unique<LTObject>();
    near->m_Flags = far->m_Flags = FLAG_SOLID;
    near->m_BBoxMin.Init(-1,19,-1); near->m_BBoxMax.Init(1,21,1);
    far->m_BBoxMin.Init(-1,-1,-1); far->m_BBoxMax.Init(1,1,1);
    tree.BumpObject(near.get(),0); tree.BumpObject(far.get(),0);
    query.m_FilterFn = dispatch_object_filter;
    object_filter = [&](HOBJECT object) { if (object == far.get()) near.reset(); return LTTRUE; };
    require(w_IntersectSegment(&query,&info,&tree,0) && info.m_hObject == far.get() && info.m_Point.y == 1,
            "closest surviving box wins when a later filter destroys an earlier candidate");
    object_filter = {}; query.m_FilterFn = nullptr;
    query.m_From.Init(); query.m_To.Init(0,10,0);
    require(!w_IntersectSegment(&query,&info,&tree,0),"ordinary boxes preserve the original inside-start exclusion");
    query.m_To = query.m_From;
    require(!w_IntersectSegment(&query,&info,&tree,0) && !info.m_hObject,"degenerate segment returns initialized empty output");
}
void world_startup(const char* module_path) {
    const auto root = std::filesystem::temp_directory_path() / ("nolf-world-start-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::filesystem::create_directories(root / "worlds");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path,error); } } cleanup{root};
    // Minimal version-66 world fixture: independent serialized bounds, one
    // spatial root, no models/objects, and empty light-animation tables.
    std::vector<std::byte> bytes(44);
    auto append = [&]<class T>(T value) { const auto at = bytes.size(); bytes.resize(at+sizeof(T)); nolf::wire::store(bytes.data()+at,value); };
    nolf::wire::store(bytes.data(),uint32(66));
    append(uint32(0)); append(1000.f);
    for (unsigned pair = 0; pair < 2; ++pair) {
        for (unsigned axis = 0; axis < 3; ++axis) append(0.f);
        for (unsigned axis = 0; axis < 3; ++axis) append(2.f);
    }
    append(uint32(1)); append(uint32(0)); append(uint8(0)); append(uint32(0));
    nolf::wire::store(bytes.data()+4,uint32(bytes.size())); append(uint32(0));
    nolf::wire::store(bytes.data()+8,uint32(bytes.size()));
    for (unsigned word = 0; word < 5; ++word) append(uint32(0));
    nolf::platform::atomic_write(root / "worlds/test.dat",bytes);
    nolf::platform::Module observer(module_path);
    auto set_hook = reinterpret_cast<void (*)(void (*)(uint32))>(observer.symbol("test_module_shell_phase_hook"));
    struct ResetHook { decltype(set_hook) set; ~ResetHook() { set(nullptr); shell_phase_hook = {}; } } reset_hook{set_hook};
    auto setup = [&] {
        auto server = std::make_unique<CServerMgr>();
        require(server->Init(),"world-start fixture server initializes");
        server->m_FileMgr.mount(root); server->m_ClassMgr.configure(module_path);
        require(server->m_ClassMgr.load(server->m_pUnknown928) == LT_OK,"world-start fixture module loads");
        return server;
    };
    auto server = setup();
    auto start = [&](char* name, uint32 flags) { return server->DoStartWorld(name,flags,0); };
    require(start(const_cast<char*>("Worlds\\Test"),3) == LT_OK && server->m_Unknown198 == 0 && server->m_pWorldStream,
            "complete native world startup mounts and runs a version-66 world");
    const auto* geometry = server->m_World.Geometry(); const auto* stream = server->m_pWorldStream;
    require(start(const_cast<char*>("worlds/missing"),3) == LT_MISSINGWORLDFILE && server->m_World.Geometry() == geometry &&
            server->m_pWorldStream == stream && server->m_Unknown198 == 0,"missing world preserves the running world before callbacks");
    require(start(const_cast<char*>("worlds/test.dat"),7) == LT_OK && server->m_World.Geometry() == geometry && !server->m_StartingWorld,
            "same-world restart retains geometry and replaces owned stream");
    std::string too_long(300,'x');
    require(start(too_long.data(),3) == LT_INVALIDPARAMS && start(nullptr,3) == LT_INVALIDPARAMS &&
            start(const_cast<char*>("worlds/test"),8) == LT_INVALIDPARAMS,"world startup validates bounded names and flags");
    require(start(const_cast<char*>("worlds/test"),1) == LT_OK && server->m_Unknown198 == 1 && server->DoRunWorld() == LT_OK &&
            server->DoRunWorld() == LT_OK,"paused world loads and RunWorld is idempotent");
    unsigned ticks = 0;
    shell_phase_hook = [&](uint32 phase) { if (phase == 4) { ++ticks; require(!server->Update(0,1),"recursive frame update is rejected"); } };
    set_hook(dispatch_shell_phase);
    require(server->Update(0,.005f) && ticks == 0 && server->m_Time == 0,"sub-10ms host frame processes without accelerating game time");
    require(server->Update(0,.02f) && ticks == 1 && std::fabs(server->m_Time-.02f) < .00001f,"native frame advances by actual elapsed time");
    require(server->Update(1,.1f) && ticks == 1,"paused native frame does not simulate");
    server->m_Unknown1a0 |= 4;
    require(server->Update(0,.2f) && ticks == 1,"unavailable demo frame returns without blocking the engine thread");
    server->m_Unknown6dc = 1;
    require(server->Update(0,.2f) && ticks == 2 && !server->m_Unknown6dc,"one queued demo frame permits one simulation tick");
    server->m_Unknown1a0 &= ~uint32(4);
    const auto saved_rate = g_UpdatesPerSec;
    for (float invalid : {0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        g_UpdatesPerSec = invalid; require(!server->Update(0,.3f),"invalid update frequency is rejected before integer conversion");
    }
    g_UpdatesPerSec = saved_rate;
    require(!server->Update(0,std::numeric_limits<float>::infinity()),"nonfinite host time is rejected");
    require(server->Update(0,std::numeric_limits<float>::max()) && std::isfinite(server->m_Time),"large finite time rebases counters without overflowing conversions");
    require(start(const_cast<char*>("worlds/test"),3) == LT_OK,"level switch fixture rebases host time");
    shell_phase_hook = [&](uint32 phase) {
        if (phase == 4) {
            set_hook(nullptr); shell_phase_hook = {};
            require(start(const_cast<char*>("worlds/test"),3) == LT_OK,"game update can complete a level transition");
        }
    };
    require(server->Update(0,.02f) && server->m_bWorldLoaded && server->m_Time == 0 && !server->m_Updating,
            "successful world transition cancels the old frame and preserves the running server");
    set_hook(nullptr); shell_phase_hook = {};
    server.reset();
    for (uint32 phase = 0; phase < 4; ++phase) for (unsigned action = 0; action < 3; ++action) {
        server = setup();
        shell_phase_hook = [&](uint32 current) {
            if (current != phase) return;
            set_hook(nullptr); shell_phase_hook = {};
            require(start(const_cast<char*>("worlds/test"),3) == LT_ERROR,"nested world transition is rejected during a game callback");
            if (action == 0) server->EndWorld(0);
            else if (action == 1) server->Term();
            else server.reset();
        };
        set_hook(dispatch_shell_phase);
        require(start(const_cast<char*>("worlds/test"),3) != LT_OK,"game callback retirement aborts the previous world startup");
        if (server) require(!server->m_StartingWorld && !server->m_RunningWorldStart,"retired world startup releases transition guards");
        set_hook(nullptr); shell_phase_hook = {}; server.reset();
    }
    for (bool from_actor : {false,true}) for (unsigned action = 0; action < 3; ++action) {
        server = setup();
        require(start(const_cast<char*>("worlds/test"),3) == LT_OK,"frame retirement fixture starts");
        auto retire = [&] {
            set_hook(nullptr); shell_phase_hook = {}; engine_hook = {};
            if (action == 0) server->EndWorld(0);
            else if (action == 1) server->Term();
            else server.reset();
        };
        if (from_actor) {
            ObjectCreateStruct create;
            auto* actor = server->CreateObject(reinterpret_cast<HCLASS>(server->m_ClassMgr.find("NativeTestObject")),&create);
            require(actor,"frame fixture actor creates");
            server->m_pUnknown928->SetNextUpdate(actor->m_hObject,.01f);
            auto hook = reinterpret_cast<void (*)(void (*)(BaseClass*,uint32,void*,float))>(observer.symbol("test_module_hook"));
            engine_hook = [&](BaseClass*,uint32 id,void*,float) { if (id == MID_UPDATE) retire(); };
            hook(dispatch_hook);
            require(!server->Update(0,.02f),"actor callback retirement stops the active frame");
            hook(nullptr); engine_hook = {};
        } else {
            shell_phase_hook = [&](uint32 phase) { if (phase == 4) retire(); };
            set_hook(dispatch_shell_phase);
            require(!server->Update(0,.02f),"shell callback retirement stops the active frame");
        }
        if (server) require(!server->m_Updating,"frame guard releases after world retirement");
        server.reset();
    }
}
void checks() {
    std::shared_ptr<ServerLifetime> lifetime;
    {
        CServerMgr never_started; lifetime = never_started.m_Lifetime;
        require(never_started.m_Time == 0 && !never_started.m_pUnknown928 && !never_started.m_Initialized,"native server constructor defines scalar and owned-pointer state");
        require(!never_started.m_ForceControl.m_pObj && !never_started.m_ForceControl.m_pVelocity &&
            !never_started.m_ForceControl.m_pAcceleration && never_started.m_ForceControl.m_fFrameTime == 0,
            "initial force request can be safely snapshotted before the first physics callback");
        never_started.Term(); never_started.Term();
    }
    require(!lifetime->alive,"native server destruction invalidates independent callback lifetime");
    CServerMgr server;
    require(server.Init() && server.m_Initialized && server.m_pUnknown928,"native server initializes real SDK and worker services");
    auto* sdk = server.m_pUnknown928;
    require(server.Init() && server.m_pUnknown928 == sdk,"native server initialization is idempotent");
    std::array<std::byte,4> information{std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
    require(server.SetGameInfo(information.data(),information.size()),"game information owns a staged copy");
    information[0] = std::byte{9};
    require(server.m_GameInformation.front() == std::byte{1} &&
            server.SetGameInfo(server.m_GameInformation.data(),server.m_GameInformation.size()) &&
            !server.SetGameInfo(nullptr,4) && server.m_GameInformation.size() == 4 && server.SetGameInfo(nullptr,0) && server.m_GameInformation.empty(),
            "game information handles aliasing, invalid input and explicit empty replacement without null copies");
    for (uint16 id = 0; id < 257; ++id) sm_BroadcastCacheFile(&server,id % 4,id);
    sm_BroadcastCacheFile(&server,3,17);
    require(server.m_CachedFiles.size() == 257,"native file-cache inventory grows and deduplicates stable file identities");
    for (uint16 id = 0; id < 257; ++id)
        require(server.m_CachedFiles[id].file_id == id && server.m_CachedFiles[id].type == id % 4,"file-cache growth preserves every typed entry");
    require(sm_StartWorldA(&server,1) == LT_OK && server.m_CachedFiles.empty(),"new world caching pass clears the prior inventory");
    char optional[]="";
    for (auto type : {FT_MODEL,FT_SPRITE,FT_TEXTURE,FT_SOUND})
        require(sdk->CacheFile(type,optional)==LT_OK && server.m_CachedFiles.empty(),"empty optional preload registered an invalid asset");
    ObjectList empty{nullptr,0};
    con_SetVar(&server.m_ConsoleState,"SaveProbe","a; b \" c % d");
    server.m_Time = 25.5f; server.m_Unknown6c0 = 30;
    nolf::MemoryStream saved({},true);
    require(sav_SaveObjects(&server,&saved,&empty,77,3) == LT_OK,"actual server captures a staged save document");
    auto bytes = std::vector<std::byte>(saved.data().begin(),saved.data().end());
    auto document = nolf::DecodeSavedGame(bytes);
    require(document.objects.empty() && document.portal_flags && document.portal_flags->empty(),"actual server encodes absent-world portal table explicitly");
    con_SetVar(&server.m_ConsoleState,"SaveProbe","changed"); server.m_Time = 0;
    nolf::MemoryStream input(bytes);
    require(sav_RestoreObjects(&server,&input,77,RESTOREOBJECTS_RESTORETIME) == LT_OK && server.m_Time == 25.5f &&
        console(server,"SaveProbe") == "a; b \" c % d" && !server.m_Restoring,"actual server restores timing and literal console values without command parsing");
    auto failed = document;
    failed.console_variables = {{"SaveProbe","partial"},{"AddedDuringFailedRestore","temporary"},{"","invalid"}};
    server.m_Time = 71;
    nolf::MemoryStream failing(nolf::EncodeSavedGame(failed));
    require(sav_RestoreObjects(&server,&failing,77,RESTOREOBJECTS_RESTORETIME) != LT_OK && server.m_Time == 71 &&
        console(server,"SaveProbe") == "a; b \" c % d" && !con_FindVar(&server.m_ConsoleState,"AddedDuringFailedRestore") && !server.m_Restoring,
        "failed restore rolls back native time, console variables and operation state");
    bytes[0] ^= std::byte{1}; nolf::MemoryStream wrong_version(bytes);
    require(sav_RestoreObjects(&server,&wrong_version,0,1) == LT_INVALIDVERSION && server.m_Time == 71,"unsupported version is rejected before live state changes");
    // Save names are logical game paths under the user data root.
    const ScopedUserHome home;
    const auto path = home.data() / "save" / std::filesystem::path(u8"Сохранение.dat");
    const std::string name = "Save\\" + std::string(reinterpret_cast<const char*>(u8"Сохранение.dat"));
    require(sav_SaveFile(&server,name.c_str(),&empty,0,1) == LT_OK,"native server save atomically publishes a Unicode file");
    require(std::filesystem::is_regular_file(path),"Unicode save not under the user data root");
    const auto before = nolf::platform::read_file(path);
    ObjectLink invalid{reinterpret_cast<HOBJECT>(std::uintptr_t(1)),nullptr}; ObjectList bad{&invalid,1};
    require(sav_SaveFile(&server,name.c_str(),&bad,0,1) != LT_OK && nolf::platform::read_file(path) == before,"failed native save preserves original destination bytes");
    con_SetVar(&server.m_ConsoleState,"SaveProbe","after file");
    require(sav_RestoreFile(&server,name.c_str(),0,1) == LT_OK && console(server,"SaveProbe") == "a; b \" c % d","native server file restore uses owned platform stream");
    server.Term(); server.Term();
    require(!server.m_Initialized && !server.m_pUnknown928 && !server.m_Thread.m_hThread.joinable(),"native server teardown releases worker and SDK services");
    require(server.Init() && server.m_Initialized,"native server services restart after teardown");
}
void packets(const char* module_path) {
    CServerMgr server;
    require(server.Init(),"packet fixture server initializes");
    server.m_ClassMgr.configure(module_path);
    require(server.m_ClassMgr.load(server.m_pUnknown928) == LT_OK,"packet fixture loads real SDK shell");
    Client client;
    require(client.Initialize(200),"packet fixture owns native client state");
    const auto deliver = [&](unsigned id,std::initializer_list<uint8> payload) {
        ServerMessage packet;
        packet.m_pData[0] = static_cast<uint8>(id);
        if (payload.size()) packet.WriteData(payload.begin(),payload.size());
        packet.m_Pos = 1;
        require(g_PacketHandlers[id](&server,&packet,&client) == LT_OK,"native typed packet dispatch returns normally");
        return packet.m_Pos;
    };
    g_PacketHandlers[255] = g_PacketHandlers[7];
    sm_ResetProfileCounters();
    require(!g_PacketHandlers[255],"handler reset clears the full native pointer array");
    for (const auto& malformed : {std::vector<uint8>{}, {30}, {30,3,0,1}, {30,1,255}, {30,0,9}}) {
        ServerMessage packet;
        packet.m_pData[0] = 7;
        if (!malformed.empty()) packet.WriteData(malformed.data(),malformed.size());
        packet.m_Pos = 1;
        require(g_PacketHandlers[7](&server,&packet,&client) == LT_OK && packet.m_Pos == 1 &&
            client.m_nCurCommands == 0 && client.m_Unknown128 == 10,
            "invalid command packet cannot publish partial commands, rate or cursor changes");
    }
    require(deliver(7,{30,3,0,2,254,0xff,0xff}) == 8 && client.CommandOn(0) && client.CommandOn(2) &&
        client.CommandOn(254) && !client.CommandOn(1) && client.m_Unknown128 == 30,
        "validated command packet publishes its complete state and consumes bounded sound tail");
    require(deliver(8,{1}) == 1,"odd sound-ID tail is ignored without a non-advancing read loop");
    require(deliver(8,{0xff,0xff}) == 3,"foreign sound IDs cannot index the client object table");
    require(deliver(11,{}) == 1 && g_PacketHandlers[11](&server,nullptr,nullptr) == LT_OK,
        "typed shell-message adapter rejects missing payloads");
    client.m_State = 2;
    require(deliver(9,{1}) == 1 && client.m_Unknown3BC == 0,"ready message cannot skip world-information delivery");
    require(deliver(9,{0}) == 2 && client.m_Unknown3BC == 1,"first valid handshake stage advances once");
    client.m_Unknown3BC = 2;
    require(deliver(9,{1,0}) == 1 && client.m_Unknown3BC == 2,"handshake validates exact payload extent");
    require(deliver(9,{1}) == 2 && client.m_Unknown3BC == 3,"second valid handshake stage advances once");
    server.Term();

    nolf::platform::Module observer(module_path);
    auto set_hook = reinterpret_cast<void (*)(void (*)(uint32))>(observer.symbol("test_module_shell_phase_hook"));
    struct Reset { decltype(set_hook) set; ~Reset() { set(nullptr); shell_phase_hook = {}; } } reset{set_hook};
    set_hook(dispatch_shell_phase);
    for (unsigned action = 0; action < 3; ++action) {
        auto owner = std::make_unique<CServerMgr>();
        require(owner->Init(),"command callback fixture initializes");
        owner->m_ClassMgr.configure(module_path);
        require(owner->m_ClassMgr.load(owner->m_pUnknown928) == LT_OK,"command callback fixture loads SDK shell");
        Client peer;
        require(peer.Initialize(200),"command callback client initializes");
        unsigned callbacks = 0;
        shell_phase_hook = [&](uint32 phase) {
            if (phase != 5) return;
            ++callbacks;
            if (action == 0) owner->EndWorld(true);
            else if (action == 1) owner->Term();
            else owner.reset();
        };
        ServerMessage packet;
        packet.m_pData[0] = 7;
        packet.WriteByte(30); packet.WriteByte(2); packet.WriteByte(0); packet.WriteByte(1); packet.WriteWord(0xffff);
        packet.m_Pos = 1;
        require(g_PacketHandlers[7](owner.get(),&packet,&peer) == LT_OK && callbacks == 1 && packet.m_Pos == 5,
            "command callbacks can end, terminate or destroy their server without processing another command or sound tail");
        shell_phase_hook = {};
    }
    struct Handler : ServerAppHandler {
        std::function<void()> callback;
        LTRESULT ProcessPacket(char*,uint32,uint8[4],uint16) override { callback(); return LT_OK; }
    } handler;
    auto owner = std::make_unique<CServerMgr>();
    require(owner->Init(),"connectionless callback fixture initializes");
    owner->m_pAppHandler = &handler;
    handler.callback = [&] { owner.reset(); };
    ServerMessage packet;
    packet.m_pData[0] = '\\';
    uint8 address[4]{127,0,0,1};
    owner->ProcessPacket(&packet,address,27888);
    require(!owner,"application packet callback can destroy its server before shell dispatch");
}
void client_entry_callbacks(const char* module_path) {
    nolf::platform::Module observer(module_path);
    auto set_hook = reinterpret_cast<void (*)(void (*)(uint32))>(observer.symbol("test_module_shell_phase_hook"));
    auto set_object = reinterpret_cast<void (*)(BaseClass*)>(observer.symbol("test_module_enter_object"));
    struct Reset {
        decltype(set_hook) hook; decltype(set_object) object;
        ~Reset() { hook(nullptr); object(nullptr); shell_phase_hook = {}; }
    } reset{set_hook,set_object};
    set_hook(dispatch_shell_phase);
    for (unsigned action = 0; action < 4; ++action) {
        auto owner = std::make_unique<CServerMgr>();
        require(owner->Init(),"client entry callback fixture initializes");
        owner->m_ClassMgr.configure(module_path);
        require(owner->m_ClassMgr.load(owner->m_pUnknown928) == LT_OK,"client entry callback fixture loads native SDK shell");
        owner->m_bWorldLoaded = 1;
        ObjectCreateStruct create;
        auto* game = owner->CreateObject(reinterpret_cast<HCLASS>(owner->m_ClassMgr.find("NativeTestObject")),&create);
        require(game,"client entry fixture creates an owned native game actor");
        set_object(game);
        Client client;
        require(client.Initialize(0xfff+40),"independent client entry record initializes");
        client.m_State = 2; client.m_Unknown3BC = 3;
        unsigned callbacks = 0;
        shell_phase_hook = [&](uint32 phase) {
            if (phase != 7) return;
            ++callbacks;
            if (action == 0) owner->EndWorld(true);
            else if (action == 1) owner->Term();
            else if (action == 2) owner.reset();
            else require(sm_UpdateClientState2(owner.get(),&client) == LT_ERROR,"recursive original player entry is rejected");
        };
        const auto result = sm_UpdateClientState2(owner.get(),&client);
        require(callbacks == 1 && !client.m_EnteringWorld,"entry flag unwinds independently of server lifetime");
        if (action < 3) require(result != LT_OK && !client.m_pUnknown3AC,"retired server cannot publish a returned player actor");
        else {
            require(result == LT_OK && client.m_State == 3 && client.m_pUnknown3AC == game->m_hObject,
                "successful SDK player entry publishes exactly one native identity");
            game->m_hObject->m_pSData->m_pOwningClient = nullptr;
            client.m_pUnknown3AC = nullptr;
        }
        set_object(nullptr); shell_phase_hook = {};
    }
    auto owner = std::make_unique<CServerMgr>();
    require(owner->Init(),"foreign entry fixture initializes");
    owner->m_ClassMgr.configure(module_path);
    require(owner->m_ClassMgr.load(owner->m_pUnknown928) == LT_OK,"foreign entry fixture loads SDK shell");
    nolf::GameModule independent(module_path,owner->m_pUnknown928);
    auto foreign = independent.create(independent.find("NativeTestObject"));
    require(foreign.get(),"foreign entry actor belongs to a separate native module owner");
    set_object(foreign.get());
    Client client;
    require(client.Initialize(200),"foreign entry client initializes");
    client.m_State = 2; client.m_Unknown3BC = 3;
    require(sm_UpdateClientState2(owner.get(),&client) != LT_OK && !client.m_pUnknown3AC && !client.m_EnteringWorld,
        "unregistered player return is rejected before dereferencing a game-owned object");
}
void public_exports(const char* module_path) {
    LTGUID guid{}; ServerInterface* api = reinterpret_cast<ServerInterface*>(std::uintptr_t(1));
    require(CreateServer(SI_VERSION,guid,nullptr) == SI_ERRORINITTING,"public server creation rejects missing output");
    require(CreateServer(SI_VERSION+1,guid,&api) == SI_INVALIDVERSION && !api,"public server export validates SDK version");
    require(CreateServer(SI_VERSION,guid,&api) == SI_CANTLOADRESOURCEMODULE && !api,"public server requires a configured native resource catalogue");
    const auto root = std::filesystem::temp_directory_path() / ("nolf-server-api-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { DeleteServer(); nolf::configure_engine_resources(nullptr); std::error_code error; std::filesystem::remove_all(path,error); }
    } cleanup{root};
    std::filesystem::create_directories(root / "resources/ltmsg/1033/strings");
    const std::string message = "Engine error %1!d!.";
    nolf::platform::atomic_write(root / "resources/ltmsg/1033/strings/1.txt",std::as_bytes(std::span(message)));
    nolf::configure_engine_resources(std::make_shared<nolf::Resources>(root));
    require(CreateServer(SI_VERSION,guid,&api) == SI_OK && api,"public server export initializes the full native manager");
    ServerInterface* duplicate = api;
    require(CreateServer(SI_VERSION,guid,&duplicate) == SI_ALREADYINSTANCED && !duplicate,"public server publishes one complete instance");
    char command[] = "PublicProbe 42";
    require(api->RunConsoleString(command) == LT_OK,"public server interface dispatches native console operation");
    DeleteServer(); DeleteServer();
    require(api->RunConsoleString(command) == LT_NOTINITIALIZED && !g_pServerMgr && !g_pClassMgr,"public server deletion detaches the interface and resets ownership");
    require(CreateServer(SI_VERSION,guid,&api) == SI_OK && api,"public server export restarts after deletion");
    nolf::platform::Module observer(module_path);
    auto set_hook = reinterpret_cast<void (*)(void (*)(uint32))>(observer.symbol("test_module_shell_phase_hook"));
    auto* manager = g_pServerMgr;
    manager->m_ClassMgr.configure(module_path);
    require(manager->m_ClassMgr.load(manager->m_pUnknown928) == LT_OK,"public update fixture loads SDK shell");
    manager->m_Unknown198 = 0;
    // The next update ticks however soon it follows the load (10ms minimum).
    manager->m_Unknown6d8 = -1;
    const auto lifetime = manager->m_Lifetime;
    shell_phase_hook = [&](uint32 phase) {
        if (phase != 4) return;
        require(g_ServerInterface.m_bInUpdate && !api->Update(0),"public update rejects recursion without clearing the outer activity flag");
        DeleteServer(); DeleteServer();
        require(!g_ServerInterface.m_pServerMgr && lifetime->alive && lifetime->retiring && lifetime->active_calls >= 2,
                "public deletion detaches immediately and keeps the active manager alive until return");
        ServerInterface* replacement = nullptr;
        require(CreateServer(SI_VERSION,guid,&replacement) == SI_ALREADYINSTANCED && !replacement,
                "a retiring callback cannot replace the active public manager");
        require(api->RunConsoleString(command) == LT_NOTINITIALIZED,"detached public interface rejects new work during retirement");
    };
    set_hook(dispatch_shell_phase);
    require(!api->Update(0),"public deletion cancels the active frame");
    set_hook(nullptr); shell_phase_hook = {};
    require(!lifetime->alive && lifetime->active_calls == 0 && !lifetime->when_idle && !g_ServerInterface.m_bInUpdate &&
            !g_pServerMgr && !g_pClassMgr,"public manager deletes only after the outer update activity unwinds");
    ILTStream* stream = reinterpret_cast<ILTStream*>(std::uintptr_t(1));
    require(api->OpenFile(command,&stream) == LT_NOTINITIALIZED && !stream && !api->InitNetworking(nullptr,0) &&
            !api->SetGameInfo(nullptr,0),"detached public file, network and game-info entry points fail safely");
    require(CreateServer(SI_VERSION,guid,&api) == SI_OK,"public server restarts after deferred owner destruction");
}
}
int main(int argc,char** argv) {
    try { require(argc == 2 || argc == 3,"expected native SDK fixture module and optional local assets"); checks(); segment_queries(); world_startup(argv[1]); actors(argv[1]); packets(argv[1]); client_entry_callbacks(argv[1]); if (argc == 3) original_worlds(argv[1],argv[2]); public_exports(argv[1]); std::cout << "native server startup, live actors, save adapters and teardown tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
