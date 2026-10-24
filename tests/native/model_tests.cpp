#include "model_fixture.h"
#include "engine/server/commonmodel.h"
#include "engine/server/model_loading.h"
#include "engine/server/model_file.h"
#include "engine/server/model_cache.h"
#include "engine/server/model_service.h"
#include "engine/server/model_callback.h"
#include "ltserverobj.h"
#include "ltengineobjects.h"
#include "engine/server/filemgr.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/memory_stream.h"
#include <bit>
#include <array>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <thread>
#include <future>
#include <condition_variable>
#include "platform/system.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>

ILTServer* g_pLTServer = nullptr;

namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool near(float a,float b) { return std::abs(a-b) < .001f; }
void skeleton(Model& model,uint32 count) {
    auto* node = model.m_pSub20c;
    for (uint32 i = 1; i < count; ++i) {
        require(node->m_Array18.SetSize(1),"allocate child index");
        node->m_Array18[0] = BaseNew(&g_DefAlloc,static_cast<ModelNode*>(nullptr),1,&model);
        node = node->m_Array18[0];
    }
    require(model.sub_1001728c(1) && model.sub_100172e3(1),"native skeleton index");
    require(model.m_Sub1d4.m_Array00.SetSize(count),"child bind pose");
    for (uint32 i = 0; i < count; ++i) {
        model.m_Sub1d4.m_Array00[i].m_Pos.Init();
        model.m_Sub1d4.m_Array00[i].m_Rot.Init();
    }
}
void add_animation(Model& model,uint32 index, float translation) {
    model.m_AnimArray[index].m_pAnim = BaseNew(&g_DefAlloc,static_cast<ModelAnim*>(nullptr),1,&model);
    auto& anim = *model.m_AnimArray[index].m_pAnim;
    model.m_AnimArray[index].m_pChildModel = &model.m_Sub1d4;
    require(anim.m_Array08.SetSize(3),"native keyframes");
    anim.m_Array08[0].m_Time = 0; anim.m_Array08[1].m_Time = 100; anim.m_Array08[2].m_Time = 200;
    anim.m_Array08[0].m_pString = const_cast<char*>("start");
    anim.m_Array08[1].m_pString = const_cast<char*>("middle");
    anim.m_Array08[2].m_pString = const_cast<char*>("end");
    auto* current = &anim.m_Sub28;
    for (uint32 i = 0; i < model.m_Nodes.GetSize(); ++i) {
        current->m_pSub20c04 = model.m_Nodes[i];
        require(current->m_Array08.SetSize(3),"node frames");
        for (uint32 j = 0; j < 3; ++j) {
            current->m_Array08[j].m_Pos.Init(translation + float(j),0,0);
            current->m_Array08[j].m_Rot.Init();
        }
        if (i + 1 < model.m_Nodes.GetSize()) {
            require(current->m_Array1c.SetSize(1),"animation child");
            current->m_Array1c[0] = BaseNew(&g_DefAlloc,static_cast<ModelAnimationNode*>(nullptr),1,&anim,current);
            current = current->m_Array1c[0];
        }
    }
    require(anim.sub_100168a3(1),"flatten animation tree");
}
void count_key(LTAnimTracker* tracker,AnimKeyFrame*) { ++*static_cast<unsigned*>(tracker->m_pUser1); }
void reset_key(LTAnimTracker* tracker,AnimKeyFrame*) { at_ResetAnim(tracker); }
class KeyReceiver final : public BaseClass {
public:
    std::vector<std::vector<std::string>> commands;
    std::function<void()> action;
    uint32 EngineMessageFn(uint32 message, void* payload, float value) override {
        require(message == MID_MODELSTRINGKEY && value == 0, "native SDK model-key message");
        auto& args = *static_cast<ArgList*>(payload);
        std::vector<std::string> command;
        for (int i = 0; i < args.argc; ++i) command.emplace_back(args.argv[i]);
        commands.push_back(std::move(command));
        auto once = std::move(action);
        if (once) once();
        return 1;
    }
};
void string_key_callbacks() {
    CommonModelLT api;
    auto make_model = [] {
        auto model = std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc);
        skeleton(*model,1); require(model->m_AnimArray.SetSize(1),"callback animation slot");
        add_animation(*model,0,0);
        char script[] = "first \"two words\"; ;second three";
        model->m_AnimArray[0].m_pAnim->m_pKeyFrames[0].m_pString = model->sub_100173fa(script);
        return model;
    };
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto model = make_model();
        std::weak_ptr<Model> weak = model;
        auto object = std::make_unique<ModelInstance>();
        object->BindModel(model); model.reset();
        require((object->m_AnimTracker.m_Flags & AT_PLAYING) != 0,"initial model binding starts animation");
        KeyReceiver receiver;
        SObjData data{}; data.m_pBaseClass = &receiver;
        object->m_pSData = &data; object->m_Flags |= FLAG_MODELKEYS;
        object->m_AnimTracker.m_StringKeyCallback = sm_ModelStringKeyCallback;
        auto extra = std::make_unique<LTAnimTracker>();
        require(api.AddTracker(object.get(),extra.get()) == LT_OK,"callback secondary tracker");
        extra->m_StringKeyCallback = sm_ModelStringKeyCallback;
        switch (scenario) {
        case 0: break;
        case 1: receiver.action = [&] { at_ResetAnim(&object->m_AnimTracker); }; break;
        case 2: receiver.action = [&] { object->BindModel({}); require(weak.expired(),"callback releases original model"); }; break;
        case 3: receiver.action = [&] { object->m_ChangeFlags |= 1; }; break;
        case 4: receiver.action = [&] { object->m_Flags &= ~FLAG_MODELKEYS; }; break;
        case 5: receiver.action = [&] { require(api.RemoveTracker(object.get(),extra.get()) == LT_OK,"callback removes current tracker"); extra.reset(); }; break;
        case 6: receiver.action = [&] { object.reset(); require(weak.expired(),"callback destroys model instance and model"); }; break;
        case 7: receiver.action = [&] { require(api.RemoveTracker(object.get(),extra.get()) == LT_OK,"callback removes later tracker"); extra.reset(); }; break;
        }
        if (scenario == 5) at_UpdateTracker(extra.get(),1);
        else if (scenario == 6 || scenario == 7) model_UpdateTrackers(*object,1,sm_ModelStringKeyCallback);
        else at_UpdateTracker(&object->m_AnimTracker,1);
        const auto expected = scenario == 0 || scenario == 7 ? 2u : 1u;
        if (receiver.commands.size() != expected)
            throw std::runtime_error("model-key callback scenario " + std::to_string(scenario) +
                " delivered " + std::to_string(receiver.commands.size()) + " commands, expected " + std::to_string(expected));
        require(receiver.commands[0] == std::vector<std::string>({"first","two words"}),"SDK ArgList preserves quoted words");
        if (receiver.commands.size() == 2) require(receiver.commands[1] == std::vector<std::string>({"second","three"}),"SDK ArgList multiple commands");
        if (object && extra) require(api.RemoveTracker(object.get(),extra.get()) == LT_OK,"detach fixture tracker");
    }
    LTAnimTracker raw;
    require(!raw.GetModelInstance() && !raw.GetNext(),"standalone tracker has no uninitialized owner");
    sm_ModelStringKeyCallback(nullptr,nullptr);
}
void node_callback(HOBJECT object,HMODELNODE,LTMatrix*,void* data) {
    require(object && object->m_ObjectType == OT_MODEL,"node callback retains native object identity");
    ++*static_cast<unsigned*>(data);
}
void tracker_and_evaluator() {
    Model empty(&g_DefAlloc,&g_DefAlloc);
    LTAnimTracker invalid{}; at_Init(&invalid,&empty,0);
    require(!invalid.IsValid(),"empty models have no valid animation");
    at_UpdateTracker(&invalid,UINT32_MAX); at_UpdateTracker(&invalid,42);
    require(invalid.m_TimeRef.m_Cur.m_Time == UINT32_MAX,"invalid time saturates without wrapping");
    require(!invalid.GetCurAnim() && !invalid.GetCurFrame(),"empty model accessors are bounded");
    Model model(&g_DefAlloc,&g_DefAlloc);
    skeleton(model,200);
    require(model.m_AnimArray.SetSize(2),"animation slots");
    add_animation(model,0,1); add_animation(model,1,10);
    LTAnimTracker tracker{}; at_Init(&tracker,&model,0);
    unsigned keys = 0; tracker.m_pUser1 = &keys; tracker.m_StringKeyCallback = count_key;
    at_UpdateTracker(&tracker,50);
    require(keys == 1 && near(tracker.m_TimeRef.m_Percent,.5f) && tracker.m_TimeRef.IsValid(),"keys and bracketing interpolation");
    ModelWalker walker; walker.m_Unknown_e0 = 1; tracker.SetupTimeRef(&walker.m_TimeRefs[0]);
    require(walker.Evaluate(199),"evaluate a skeleton deeper than the old 64-node path");
    require(near(model.m_NodeTransforms[199].m[0][3],300),"all 200 ancestor transforms contribute");
    at_SetCurAnim(&tracker,1,LTTRUE);
    at_UpdateTracker(&tracker,100);
    require(near(tracker.m_TimeRef.m_Percent,.5f),"animation transition interpolates");
    at_UpdateTracker(&tracker,70000);
    require(tracker.IsValid() && tracker.m_InterpolationMS == 0xffff && tracker.m_TimeRef.IsValid(),"large delta completes transition without 16-bit wrap");
    tracker.m_Flags &= ~AT_LOOPING;
    at_UpdateTracker(&tracker,UINT32_MAX);
    require(at_IsDone(&tracker) && tracker.GetCurFrame() == &model.m_AnimArray[1].m_pAnim->m_pKeyFrames[2],"finished animation remains on a valid final frame");
    at_ResetAnim(&tracker); tracker.m_StringKeyCallback = reset_key;
    at_UpdateTracker(&tracker,150);
    require(tracker.m_CurKey == 0 && tracker.m_TimeRef.m_Cur.m_Time == 0,"callback reset cancels the old advancement");
    auto ref = tracker.m_TimeRef; ref.m_Percent = std::numeric_limits<float>::quiet_NaN();
    require(!ref.IsValid(),"NaN blend rejected");
    require(!tracker.m_TimeRef.SetKeyframePosition(0,65536,0,0),"oversized wire frame rejected before narrowing");
    require(model.m_WeightSets.SetSize(1),"weight slot");
    model.m_WeightSets[0] = BaseNew(&g_DefAlloc,static_cast<ModelWeightSet*>(nullptr),1,&model);
    auto& weights = model.m_WeightSets[0]->m_Weights;
    require(weights.SetSize(200),"per-node weights");
    for (uint32 i = 0; i < 200; ++i) weights[i] = .5f;
    walker.m_Unknown_e0 = 2;
    walker.m_TimeRefs[0].Init(&model,0,0,0,0,0);
    walker.m_TimeRefs[1].Init(&model,1,0,1,0,0); walker.m_TimeRefs[1].m_iWeightSet = 0;
    require(walker.sub_100386c6() && near(model.m_NodeTransforms[199].m[0][3],1100),"multiple animation references use native float weights");
    ModelInstance instance;
    at_Init(&instance.m_AnimTracker,&model,0);
    unsigned callbacks = 0;
    instance.m_Unknown1fc = node_callback; instance.m_Unknown200 = &callbacks;
    ModelWalker controlled; instance.SetupModelWalker(&controlled);
    require(controlled.sub_100386c6() && callbacks == 200,"native callback and user-data pointers survive model-instance setup");
    model.m_Nodes[0]->m_iParent = 199;
    require(!walker.Evaluate(199),"cyclic or forward parent rejected");
    model.m_Nodes[0]->m_iParent = UINT32_MAX;
    std::string long_name(4096,'n');
    auto* stored = model.sub_100173fa(long_name.data());
    require(long_name == stored && stored == model.sub_100173fa(long_name.data()),"native string headers preserve and intern long names");
}
using nolf_test::Bytes;
using nolf_test::tiny_model;
class FailingAllocator final : public LAlloc {
public:
    explicit FailingAllocator(unsigned fail) : fail_(fail) {}
    void* Alloc(uint32 size) override {
        if (++calls_ == fail_) return nullptr;
        auto* block = std::malloc(size);
        if (block) live_.insert(block);
        return block;
    }
    void Free(void* block) override {
        if (!block) return;
        require(live_.erase(block) == 1,"native model frees through its owning allocator exactly once");
        std::free(block);
    }
    std::size_t live() const { return live_.size(); }
private:
    unsigned calls_ = 0, fail_;
    std::unordered_set<void*> live_;
};
void bounded_files() {
    const auto bytes = tiny_model();
    std::string reason;
    require(model_ValidateFile(bytes,&reason),"synthetic complete ABC file validated");
    Model model(&g_DefAlloc,&g_DefAlloc);
    nolf::MemoryStream stream(bytes);
    require(model.Load(&stream,&g_DefAlloc) == LT_OK,"model header uses native allocation sizes");
    CModelLoadRequest request; request.m_pStream = &stream; request.m_bUnknown10 = LTFALSE;
    require(model.LoadChildModels(&request) == LT_OK,"validated ABC constructs actual model records");
    require(model.m_AnimArray.GetSize() == 1 && model.m_Nodes.GetSize() == 1,"synthetic ABC content");
    for (std::size_t length = 0; length < bytes.size(); ++length)
        require(!model_ValidateFile(std::span(bytes).first(length)),"every truncated ABC rejected");
    auto corrupt = bytes; corrupt[8] = std::byte{0}; corrupt[9] = std::byte{0}; corrupt[10] = std::byte{0}; corrupt[11] = std::byte{0};
    nolf::MemoryStream bad(corrupt); request.m_pStream = &bad;
    require(model.LoadChildModels(&request) == LT_INVALIDFILE && model.m_AnimArray.GetSize() == 1,
            "bad chunk link rejected before replacing current model");
    {
        FailingAllocator allocator(0);
        {
            Model owner(&allocator,&allocator);
            ModelPieceData data(&owner);
            require(data.m_Array00.SetSize2(2,&allocator) && data.m_Array10.SetSize2(1,&allocator),"piece-data arrays use owner allocator");
            data.m_Array00[0].replacement = 17;
            ModelPieceData copy(data);
            require(copy.m_Array00.m_pArray != data.m_Array00.m_pArray && copy.m_Array00[0].replacement == 17,"piece-data copy owns distinct native storage");
            ModelPieceData assigned; assigned = data;
            require(assigned.m_pModel == &owner && assigned.m_Array10.GetSize() == 1,"piece-data assignment transfers allocator ownership with its storage");
        }
        require(allocator.live() == 0,"copied piece data releases through original allocator");
    }
    for (unsigned failure = 1; failure < 50; ++failure) {
        FailingAllocator allocator(failure);
        {
            Model failed(&allocator,&allocator);
            nolf::MemoryStream source(bytes); request.m_pStream = &source;
            failed.LoadChildModels(&request);
        }
        require(allocator.live() == 0,"failed model load releases every partial native allocation");
    }
    uint32 random = 123456;
    for (unsigned i = 0; i < 2000; ++i) {
        auto mutation = bytes; random = random*1664525+1013904223;
        mutation[random%mutation.size()] = std::byte(random >> 24);
        nolf::MemoryStream fuzz(mutation); request.m_pStream = &fuzz;
        Model decoded(&g_DefAlloc,&g_DefAlloc);
        const auto result = decoded.LoadChildModels(&request);
        require(result == LT_OK || result == LT_INVALIDFILE || result == LT_ERROR,"malformed model has a defined outcome");
    }
}
void cache_lifetime() {
    struct Directory {
        std::filesystem::path path;
        Directory() {
            for (unsigned i = 0; i < 100; ++i) {
                path = std::filesystem::temp_directory_path() / ("nolf-models-"+std::to_string(nolf::platform::monotonic_nanoseconds())+"-"+std::to_string(i));
                if (std::filesystem::create_directory(path)) return;
            }
            throw std::runtime_error("Cannot create model fixture directory");
        }
        ~Directory() { std::error_code error; std::filesystem::remove_all(path,error); }
    } directory;
    nolf::platform::atomic_write(directory.path/"child.abc",tiny_model());
    nolf::platform::atomic_write(directory.path/"parent.abc",tiny_model("child.abc"));
    nolf::platform::atomic_write(directory.path/"second.abc",tiny_model("child.abc"));
    nolf::platform::atomic_write(directory.path/"threaded.abc",tiny_model("child.abc"));
    nolf::platform::atomic_write(directory.path/"missing.abc",tiny_model("absent.abc"));
    nolf::platform::atomic_write(directory.path/"cyclic.abc",tiny_model("cyclic.abc"));
    for (unsigned i = 0; i < 8; ++i)
        nolf::platform::atomic_write(directory.path/("worker"+std::to_string(i)+".abc"),tiny_model("child.abc"));
    auto assets = std::make_shared<nolf::Assets>(); assets->mount_directory(directory.path);
    auto cache = std::make_unique<ModelCache>(assets);
    auto parent = cache->Load("PARENT.ABC");
    auto second = cache->Load("second.abc");
    require(parent == cache->Load("parent.abc") && parent->m_AnimArray.GetSize() == 2,"canonical cache identity and external animation append");
    require(parent->SetFilename(parent->GetFilename()) && std::string_view(parent->GetFilename()) == "parent.abc", "filename replacement can read its previous owned value");
    require(parent->m_ChildOwners[1] == second->m_ChildOwners[1] && cache->Load("child.abc") == parent->m_ChildOwners[1],"parents share child ownership and native cache identity");
    std::weak_ptr<Model> child = parent->m_ChildOwners[1];
    parent.reset(); cache->Prune();
    require(!child.expired(),"second parent retains shared child");
    for (unsigned attempt = 0; attempt < 2; ++attempt) for (auto bad : {"missing.abc","cyclic.abc"}) {
        bool rejected = false;
        try { cache->Load(bad); } catch (const std::exception&) { rejected = true; }
        require(rejected,"failed child loads reject and clear in-progress cache state for retry");
    }
    std::array<std::shared_ptr<Model>,8> results;
    std::vector<std::jthread> threads;
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&,i] { results[i] = cache->Load("threaded.abc"); });
    threads.clear();
    auto expected = results.front();
    for (auto& result : results) { require(result == expected,"concurrent cold cache loads retain identity"); result.reset(); }
    expected.reset();
    for (unsigned i = 0; i < 8; ++i) threads.emplace_back([&,i] {
        for (unsigned iteration = 0; iteration < 50; ++iteration) {
            auto temporary = cache->Load("worker"+std::to_string(i)+".abc");
            require(temporary->m_AnimArray.GetSize() == 2,"concurrent parent load/release retains shared child data");
        }
    });
    threads.clear();
    CFileMgr files;
    files.mount(directory.path);
    ModelCache file_cache([&files](std::string_view name) {
        CFileRef* file = nullptr;
        require(files.identify(name,0,&file) != 0 && file,"file manager identifies model cache input");
        return ModelCache::Source{files.locate(name),file->m_nFileID};
    });
    auto file_model = file_cache.Load("parent.abc");
    require(file_model->m_FileFlags != 0xffff && file_model->m_ChildOwners[1]->m_FileFlags != 0xffff &&
            file_model->m_FileFlags != file_model->m_ChildOwners[1]->m_FileFlags,"root and external child retain distinct engine file identifiers");
    ModelService service(files);
    auto load = service.BeginLoad("parent.abc");
    const auto ticket = load.ticket;
    std::weak_ptr<Model> pending_model = load.model;
    load.model.reset();
    require(service.HasPending("parent.abc") && !service.Find("parent.abc") && service.CollectUnused().empty(),
            "unpublished ticket owns its model independently of the result message");
    auto published = service.Complete(ticket);
    require(published && !pending_model.expired() && service.Find("PARENT.ABC") == published && !service.Complete(ticket),
            "owner publishes a load ticket exactly once");
    ModelInstance bound;
    bound.BindModel(published,false,false);
    LTAnimTracker additional{}; at_Init(&additional,published.get(),1);
    additional.SetModelInstance(&bound); additional.SetNext(nullptr); bound.m_AnimTracker.SetNext(&additional);
    bound.BindModel(published);
    published.reset();
    require(service.CollectUnused().empty(),"live model instance keeps resident model alive");
    auto cancelled = service.BeginLoad("parent.abc");
    auto old = service.Unload("parent.abc");
    require(old && bound.m_ModelOwner == old && !service.Find("parent.abc") && !service.Complete(cancelled.ticket),
            "unload cancels late publication while instance keeps old geometry alive");
    additional.m_TimeRef.m_Cur.m_iAnim = 1; additional.m_TimeRef.m_Prev.m_iAnim = 1;
    additional.m_TimeRef.m_Cur.m_Time = 37;
    unsigned pending_keys = 0;
    additional.m_StringKeyCallback = count_key; additional.m_pUser1 = &pending_keys;
    auto placeholder = std::make_shared<Model>(&g_DefAlloc,&g_DefAlloc);
    bound.BindModel(placeholder,true,true);
    require(additional.GetModel() == placeholder.get() && additional.m_TimeRef.m_Cur.m_iAnim == 1 &&
            additional.m_TimeRef.m_Cur.m_Time == 37 && (additional.m_Flags & 8),"placeholder preserves pending animation identity and elapsed time");
    auto replacement = service.BeginLoad("parent.abc");
    auto current = service.Complete(replacement.ticket);
    require(current && current != old,"reload constructs a fresh model while an old instance exists");
    bound.BindModel(current);
    require(bound.m_AnimTracker.GetModel() == current.get() && additional.GetModel() == current.get(),
            "reload rebinds every instance tracker");
    require(additional.m_TimeRef.IsValid() && additional.m_TimeRef.m_Cur.m_iAnim == 1 && !(additional.m_Flags & 8),"completion restores pending secondary animation and valid native frames");
    at_UpdateTracker(&additional,1); at_UpdateTracker(&additional,1);
    require(pending_keys == 1,"completion preserves key callback data and delivers the pending first key exactly once");
    const auto current_raw = current.get();
    current.reset(); replacement.model.reset();
    require(service.Retain(current_raw) == bound.m_ModelOwner,"service returns shared ownership for its native model identity");
    auto stale = service.BeginLoad("second.abc");
    service.Term();
    require(!service.Complete(stale.ticket) && bound.m_ModelOwner && !service.Find("parent.abc"),
            "termination invalidates tickets while live instance ownership remains independent");
    auto restart = service.BeginLoad("parent.abc");
    require(restart.ticket != stale.ticket && service.Complete(restart.ticket),"restart never reuses a stale load ticket");
    bound.BindModel({},false,false);
    bound.m_AnimTracker.SetNext(nullptr);
    require(!additional.GetModel() && !bound.m_AnimTracker.GetModel(),"clearing a model releases and clears all trackers");
    service.Term();
    const auto reserved = service.Reserve("parent.abc");
    require(service.HasPending("parent.abc") && service.Ready().empty(),"reservation is visible before worker removes its queued request");
    service.Unload("parent.abc");
    auto skipped = service.LoadReserved(reserved);
    require(!skipped.model && !service.Finish(reserved).accepted,"cancelled queued request never loads or publishes");
    auto shutdown = service.Reserve("second.abc");
    service.CancelAll();
    require(!service.LoadReserved(shutdown).model && !service.Finish(shutdown).accepted,"shutdown cancels queued tickets before joining worker");
    auto failure = service.BeginLoad("missing.abc");
    require(failure.status != LT_OK && !failure.model && service.Ready().size() == 1,"failed worker request retains an owned completion status");
    auto failed_completion = service.Finish(failure.ticket);
    require(failed_completion.accepted && failed_completion.status == failure.status && service.Ready().empty(),"failure reports once on owner thread");
    auto unqueued = service.BeginLoad("parent.abc");
    require(service.Ready().size() == 1 && service.Ready()[0].first == unqueued.ticket,"ready result survives absence of an outgoing queue message");
    bool wrong_thread = false;
    std::jthread wrong([&] { try { service.Finish(unqueued.ticket); } catch (const std::logic_error&) { wrong_thread = true; } });
    wrong.join();
    require(wrong_thread && service.Complete(unqueued.ticket),"worker cannot publish or consume owner-thread completion");
    unqueued.model.reset();
    auto pinned = service.Find("parent.abc"); pinned->m_Flags |= 1; pinned.reset();
    require(service.CollectUnused().empty(),"explicit cache pin survives unused-model collection");
    pinned = service.Find("parent.abc"); pinned->m_Flags &= ~1u; pinned.reset();
    std::weak_ptr<Model> retiring = service.Find("parent.abc");
    bool retired_before_release = false;
    auto collected = service.CollectUnused([&](std::string_view name) {
        require(!retiring.expired() && !service.Find(name),"retirement callback runs with owned model alive and metadata lock released");
        retired_before_release = true;
    });
    require(!collected.empty() && retired_before_release && retiring.expired() && !service.Find("parent.abc"),"unpin retires native indices before final model release");
    service.Term();
    struct Gate { std::mutex mutex; std::condition_variable condition; bool entered = false, released = false; } gate;
    ModelService background([&](std::string_view name) {
        if (name == "parent.abc") {
            std::unique_lock lock(gate.mutex); gate.entered = true; gate.condition.notify_all();
            gate.condition.wait(lock,[&] { return gate.released; });
        }
        CFileRef* file = nullptr; require(files.identify(name,0,&file) != 0,"background model identifier");
        return ModelCache::Source{files.locate(name),file->m_nFileID};
    });
    std::promise<ModelService::Load> answer;
    auto future = answer.get_future();
    std::jthread loader([&] { try { answer.set_value(background.BeginLoad("parent.abc")); } catch (...) { answer.set_exception(std::current_exception()); } });
    {
        std::unique_lock lock(gate.mutex);
        gate.condition.wait(lock,[&] { return gate.entered; });
    }
    require(background.HasPending("parent.abc") && !background.Find("parent.abc") && background.Ready().empty(),
            "owner queries remain responsive while resolver IO is blocked on worker");
    {
        std::lock_guard lock(gate.mutex); gate.released = true;
    }
    gate.condition.notify_all();
    auto finished = future.get(); loader.join();
    require(background.Complete(finished.ticket) && finished.model,"worker result becomes publishable only when load completes");
    background.Term();
    cache.reset(); assets.reset();
    LTAnimTracker tracker{}; at_Init(&tracker,second.get(),1);
    ModelWalker walker; walker.m_Unknown_e0 = 1; tracker.SetupTimeRef(&walker.m_TimeRefs[0]);
    require(walker.sub_100386c6(),"external animation survives cache and asset catalogue teardown");
    second.reset(); require(child.expired(),"last parent release destroys shared child exactly once");
}
void original_service(const char* path) {
    nolf::Assets catalogue; catalogue.mount_game(path);
    CFileMgr files; files.mount_game(path);
    ModelService service(files);
    uint32 models = 0; uint64 animations = 0;
    for (const auto& name : catalogue.names()) {
        if (!name.ends_with(".abc")) continue;
        {
            auto load = service.BeginLoad(name);
            require(load.status == LT_OK,"original model service load");
            auto completed = service.Finish(load.ticket);
            require(completed.accepted && completed.status == LT_OK && completed.model,"original model owner publication");
            ModelInstance instance; instance.BindModel(completed.model,false,false);
            require(instance.m_ModelOwner->m_FileFlags != 0xffff,"original model keeps engine file identifier");
            animations += completed.model->m_AnimArray.GetSize();
            for (uint32 i = 0; i < completed.model->m_AnimArray.GetSize(); ++i) {
                at_Init(&instance.m_AnimTracker,completed.model.get(),i);
                if (!instance.m_AnimTracker.IsValid()) continue;
                at_UpdateTracker(&instance.m_AnimTracker,83);
                ModelWalker walker; instance.SetupModelWalker(&walker);
                require(walker.sub_100386c6(),"original service-owned instance and external animations evaluate");
            }
            ++models;
        }
        service.CollectUnused();
    }
    service.Term();
    require(models != 0,"original model service corpus nonempty");
    std::cout << "Service models " << models << ", bound animations " << animations << ", engine file identifiers " << files.files().size() << '\n';
}
void original_cache(const char* path) {
    auto assets = std::make_shared<nolf::Assets>(); assets->mount_game(path);
    auto cache = std::make_unique<ModelCache>(assets);
    unsigned models = 0, failures = 0, children = 0;
    uint64 animations = 0;
    std::shared_ptr<Model> retained;
    for (const auto& name : assets->names()) {
        if (!name.ends_with(".abc")) continue;
        try {
            auto model = cache->Load(name);
            require(cache->Load(name) == model,"model cache preserves identity");
            children += model->m_nChildModels - 1;
            animations += model->m_AnimArray.GetSize();
            for (uint32 i = 0; i < model->m_AnimArray.GetSize(); ++i) {
                LTAnimTracker tracker{}; at_Init(&tracker,model.get(),i);
                if (!tracker.IsValid()) continue;
                at_UpdateTracker(&tracker,83);
                ModelWalker walker; walker.m_Unknown_e0 = 1; tracker.SetupTimeRef(&walker.m_TimeRefs[0]);
                require(walker.sub_100386c6(),"cached parent and external animation evaluation");
            }
            if (model->m_nChildModels > 1) retained = model;
            ++models;
            cache->Prune();
        } catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    std::cout << "Cached models " << models << ", external child references " << children << ", bound animations " << animations << ", failures " << failures << '\n';
    cache.reset(); assets.reset();
    if (retained) {
        LTAnimTracker tracker{}; at_Init(&tracker,retained.get(),retained->m_AnimArray.GetSize()-1);
        ModelWalker walker; walker.m_Unknown_e0 = 1; tracker.SetupTimeRef(&walker.m_TimeRefs[0]);
        require(walker.sub_100386c6(),"parent and child animation survive cache and catalogue teardown");
    }
    require(models && !failures,"all original child-model references resolved");
}
void original_models(const char* path) {
    nolf::Assets assets; assets.mount_game(path);
    unsigned models = 0; uint64 nodes = 0, animations = 0, frames = 0;
    for (const auto& name : assets.names()) {
        if (name.size() < 4 || name.substr(name.size()-4) != ".abc") continue;
        
        std::string reason;
        if (!model_ValidateFile(assets.read(name),&reason)) throw std::runtime_error(reason);
        Model model(&g_DefAlloc,&g_DefAlloc);
        nolf::AssetStream stream(assets.locate(name));
        CModelLoadRequest request; request.m_pStream = &stream; request.m_bUnknown10 = LTFALSE;
        require(model.Load(&stream,&g_DefAlloc) == LT_OK,"original model preflight and native allocation");
        require(model.LoadChildModels(&request) == LT_OK,"original model decoded");
        require(stream.ErrorStatus() == LT_OK,"original model complete stream");
        nodes += model.m_Nodes.GetSize(); animations += model.m_AnimArray.GetSize();
        for (uint32 i = 0; i < model.m_AnimArray.GetSize(); ++i) {
            LTAnimTracker tracker{}; at_Init(&tracker,&model,i); frames += model.m_AnimArray[i].m_pAnim->m_nKeyFrames;
            if (!tracker.IsValid()) continue;
            at_UpdateTracker(&tracker,83);
            require(tracker.m_TimeRef.IsValid(),"original animation tracking");
            ModelWalker walker; walker.m_Unknown_e0 = 1; tracker.SetupTimeRef(&walker.m_TimeRefs[0]);
            require(walker.sub_100386c6(),"original skeleton evaluation");
        }
        ++models;
    }
    require(models > 0,"original ABC models found");
    std::cout << "Original models " << models << ", nodes " << nodes << ", animations " << animations << ", frames " << frames << '\n';
}
}
int main(int argc,char** argv) {
    try {
        tracker_and_evaluator(); string_key_callbacks();
        bounded_files();
        cache_lifetime();
        if (argc == 3 && std::string_view(argv[1]) == "--data") original_models(argv[2]);
        if (argc == 3 && std::string_view(argv[1]) == "--cache-data") original_cache(argv[2]);
        if (argc == 3 && std::string_view(argv[1]) == "--service-data") original_service(argv[2]);
        std::cout << "Native model, animation and evaluation tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
