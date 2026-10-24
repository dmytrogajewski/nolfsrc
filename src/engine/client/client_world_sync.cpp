#include "engine/client/client_world_sync.h"
#include "engine/client/client_message_io.h"
#include "engine/server/ltobject_internal.h"
#include "engine/server/message.h"
#include "engine/server/commonmodel.h"
#include "iltlightanim.h"
#include "iltmodel.h"
#include "platform/logging.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nolf {
namespace {
// The engine's change flags (servermodel.h), and the two the client derives.
constexpr std::uint16_t new_object=0x1,position_changed=0x2,rotation_changed=0x4,flags_changed=0x8,scale_changed=0x10,
    model_changed=0x20,color_changed=0x40,attachments_changed=0x100,force_position=0x200,force_rotation=0x400;
[[noreturn]] void invalid(const char* what) { throw std::runtime_error(std::string("invalid server update: ")+what); }
struct Reader {
    LMessageImpl& message;
    std::size_t remaining() const { return message.m_pOwner->m_DataLen>message.m_pOwner->m_Pos ? std::size_t(message.m_pOwner->m_DataLen-message.m_pOwner->m_Pos) : 0; }
    void need(std::size_t bytes) const { if (remaining()<bytes) invalid("truncated record"); }
    std::uint8_t u8() { need(1); uint8 v=0; message.ReadByteFL(v); return v; }
    std::uint16_t u16() { need(2); uint16 v=0; message.ReadWordFL(v); return v; }
    std::uint32_t u32() { need(4); uint32 v=0; message.ReadDWordFL(v); return v; }
    float f32() { need(4); float v=0; message.ReadFloatFL(v); if (!std::isfinite(v)) invalid("non-finite value"); return v; }
    LTVector vector() { need(12); LTVector v; message.ReadVectorFL(v); return v; }
    LTVector compressed_vector() { need(1); LTVector v; message.ReadCompVectorFL(v); check(); return v; }
    LTVector compressed_position() { need(6); LTVector v; message.ReadCompPosFL(v); check(); return v; }
    LTRotation rotation() { need(16); LTRotation r; message.ReadRotationFL(r); check(); return r; }
    LTRotation compressed_rotation() { need(1); LTRotation r; message.ReadCompRotationFL(r); check(); return r; }
    std::string string() {
        const auto* data=reinterpret_cast<const char*>(message.m_pOwner->m_pData+message.m_pOwner->m_Pos);
        const auto length=strnlen(data,remaining()); if (length==remaining()) invalid("unterminated string");
        std::string text(data,length); skip(length+1); return text;
    }
    std::vector<std::byte> bytes(std::size_t count) { need(count); std::vector<std::byte> out(count); if (count) message.ReadRawFL(out.data(),uint32(count)); check(); return out; }
    void skip(std::size_t count) { need(count); message.m_pOwner->m_Pos=uint16(message.m_pOwner->m_Pos+count); }
    void check() { uint32 status=0; message.GetStatus(status); if (status) invalid("codec overflow"); }
};
// Yaw packed in one signed byte: atan2(forward.x, forward.z) * 127 / pi.
LTRotation yaw(std::uint8_t packed) {
    const double angle=double(std::int8_t(packed))*3.14159265358979323846/127.0;
    LTRotation result; result.m_Quat[0]=0; result.m_Quat[1]=float(std::sin(angle/2)); result.m_Quat[2]=0; result.m_Quat[3]=float(std::cos(angle/2));
    return result;
}
}
ClientWorldSync::ClientWorldSync(std::shared_ptr<ClientScene> scene,std::shared_ptr<const Resources> resources,std::weak_ptr<const void> engine,ClientSyncEvents events)
    : scene_(std::move(scene)),resources_(std::move(resources)),engine_(std::move(engine)),events_(std::move(events)) {
    if (!scene_ || !resources_) throw std::invalid_argument("client world sync needs its scene and resources");
}
ClientWorldSync::~ClientWorldSync() { while (!tracked_.empty()) stop(tracked_.begin()->first); }
void ClientWorldSync::reset() { while (!tracked_.empty()) stop(tracked_.begin()->first); objects_.clear(); sounds_.clear(); server_time_=0; }
HOBJECT ClientWorldSync::object(std::uint16_t id) const {
    const auto found=objects_.find(id); return found!=objects_.end() && scene_->contains(found->second.object) ? found->second.object : nullptr;
}
void ClientWorldSync::forget(HOBJECT object) {
    for (auto it=objects_.begin();it!=objects_.end();++it) if (it->second.object==object) { objects_.erase(it); return; }
}
void ClientWorldSync::apply(std::uint8_t packet,std::span<const std::byte> bytes,const ServerFiles& files) {
    if (bytes.size()>65000) invalid("oversized packet");
    ILTMessage* created=nullptr;
    if (scene_->common().CreateMessage(created)!=LT_OK || !created) throw std::bad_alloc();
    struct Release { ILTMessage* message; ~Release() { message->Release(); } } owner{created};
    auto& message=*static_cast<LMessageImpl*>(created);
    message.SetObjectIO(client_message_io(scene_,resources_,engine_));
    if (!bytes.empty() && message.WriteRaw(const_cast<std::byte*>(bytes.data()),uint32(bytes.size()))!=LT_OK) throw std::bad_alloc();
    message.ResetPos();
    Reader input{message};
    switch (packet) {
    case 8: update_packet(message,files); break;
    case 10: unguaranteed_packet(message); break;
    case 15: filenames(message,files); break;
    case 18: if (events_.special_effect) events_.special_effect(nullptr,message); break;
    // OnPortalFlagsPacket (lithtech.exe 0x0047ad20): a door opened or closed
    // a portal, or the world's open portals on entry.
    case 19: {
        const auto world=input.u16(),portal=input.u16(); const auto flags=input.u8();
        if (scene_->portal_flags(world,portal,flags)!=LT_OK) invalid("unknown portal"); break;
    }
    case 25: light_animations(message); break;
    case 26:
        light_direction_=LTVector{input.f32(),input.f32(),input.f32()};
        light_color_=LTVector{input.f32(),input.f32(),input.f32()};
        ambient_=input.f32(); break;
    default: break;   // 23/24 (file loads): nothing to apply.
    }
}
void ClientWorldSync::update_packet(LMessageImpl& message,const ServerFiles& files) {
    Reader input{message};
    while (input.remaining()) {
        std::uint16_t flags=input.u8();
        if (flags&0x80) flags=std::uint16_t((flags&0x7f)|(std::uint16_t(input.u8())<<8));
        if (flags) { object_record(message,flags,files); continue; }
        const auto type=input.u8();
        if (type==0) sound(message,files);
        else if (type==1) { const std::uint16_t change=input.u8(); const auto id=input.u16(); tracked_sound(message,id,change,files); }
        else if (type==3) removed(message);
        else invalid("unknown record type");
    }
}
void ClientWorldSync::object_record(LMessageImpl& message,std::uint16_t flags,const ServerFiles& files) {
    Reader input{message};
    const auto id=input.u16();
    HOBJECT object=nullptr; std::vector<std::byte> effect; std::uint8_t type=0; std::vector<TrackerRecord> trackers;
    if (flags&new_object) {
        if (const auto old=objects_.find(id); old!=objects_.end()) { if (scene_->contains(old->second.object)) scene_->remove(old->second.object); objects_.erase(old); }
        const auto info=input.u8(); type=std::uint8_t(info&0x1f);
        if (info&0x40) effect=input.bytes(info&0x80 ? input.u16() : input.u8());
        ObjectCreateStruct create; create.m_ObjectType=type;
        if (info&0x20) create.m_CreateFlags|=OCS_AUTOLOAD;
        const auto file=[&](std::uint16_t file_id,char* output,std::size_t capacity) {
            if (file_id==0xffff) return;
            const auto* found=files.find(file_id); if (!found) invalid("unknown file ID");
            if (found->name.size()>=capacity) invalid("oversized file name");
            std::memcpy(output,found->name.c_str(),found->name.size()+1);
        };
        if (type==OT_WORLDMODEL || type==OT_CONTAINER) {
            const auto name=input.string(); if (name.size()>=sizeof create.m_Filename) invalid("oversized world model name");
            std::memcpy(create.m_Filename,name.c_str(),name.size()+1);
            if (type==OT_CONTAINER) create.m_ContainerCode=input.u16();
        } else if (type==OT_MODEL) {
            file(input.u16(),create.m_Filename,sizeof create.m_Filename);
            for (auto& skin : create.m_SkinNames) file(input.u16(),skin,sizeof skin);
        } else if (type==OT_SPRITE) file(input.u16(),create.m_Filename,sizeof create.m_Filename);
        if (id!=0xffff) {
            const auto created=scene_->create(create,object,ClientObjectLifetime::World);
            if (created!=LT_OK || !object) {
                platform::log("client","cannot create server object "+std::to_string(id)+" type "+std::to_string(type)+" \""+create.m_Filename+"\": error "+std::to_string(created));
                object=nullptr;
            }
            else { scene_->server_id(object,id); objects_[id]={object,type}; }
        }
        flags|=force_position;
    } else {
        const auto found=objects_.find(id);
        if (found==objects_.end() || !scene_->contains(found->second.object)) invalid("update for an unknown object");
        object=found->second.object; type=found->second.type;
    }
    // A failed creation still consumes its record by the record's type.
    if (flags&model_changed) {
        if (type==OT_MODEL) trackers=animation(message,object,true);
        else if (type==OT_SPRITE) input.u32();
        else if (type!=OT_MODEL && type!=OT_SPRITE) invalid("model information for an object without one");
    }
    if (flags&flags_changed) {
        const auto low=input.u16(); const auto second=input.u8(); const auto user=input.u32();
        if (object) {
            scene_->common().SetObjectFlags(object,OFT_Flags,(object->m_Flags&0xffff0000u)|low);
            scene_->common().SetObjectFlags(object,OFT_Flags2,second);
            object->m_UserFlags=user;
        }
    }
    if (flags&color_changed) {
        const auto r=input.u8(),g=input.u8(),b=input.u8(),a=input.u8();
        if (object) scene_->color(object,r/255.f,g/255.f,b/255.f,a/255.f);
        if (type==OT_LIGHT) { const auto radius=float(input.u16()); if (object) object->m_LightRadius=radius; }
    }
    if (flags&scale_changed) {
        LTVector scale; scale.x=input.f32(); scale.y=input.f32(); scale.z=type==OT_SPRITE ? 1.f : input.f32();
        if (object) scene_->scale(object,scale);
    }
    // The flags just applied decide; a failed object has none of its own.
    const bool full=object && (object->m_Flags&FLAG_FULLPOSITIONRES);
    if (flags&(position_changed|force_position)) {
        LTVector place,velocity;
        if (full) { place.x=input.f32(); place.y=input.f32(); place.z=input.f32(); velocity=input.vector(); }
        else { place=input.compressed_position(); velocity=input.compressed_vector(); }
        if (object) { object->m_Velocity=velocity; position(object,place,(flags&(new_object|force_position))!=0); }
    }
    if (flags&(rotation_changed|force_rotation)) {
        const auto value=full ? input.rotation() : input.compressed_rotation();
        if (object) rotation(object,value,(flags&(new_object|force_position|force_rotation))!=0);
    }
    if (flags&attachments_changed) {
        while (true) {
            const auto child=input.u16(); if (child==0xffff) break;
            const auto node=input.u32(); LTVector offset; offset.x=input.f32(); offset.y=input.f32(); offset.z=input.f32();
            const auto turn=input.compressed_rotation();
            if (!object) continue;
            if (const auto target=this->object(child)) { HATTACHMENT handle=nullptr; scene_->attach(object,target,int(node),offset,turn,handle); }
        }
        if (type==OT_MODEL) {
            const auto hidden=input.u32();
            if (object && object->m_ObjectType==OT_MODEL) static_cast<ModelInstance*>(object)->m_HiddenPieces=hidden;
        }
    }
    if (object && !effect.empty() && events_.special_effect) {
        ILTMessage* created=nullptr;
        if (scene_->common().CreateMessage(created)!=LT_OK || !created) throw std::bad_alloc();
        struct Release { ILTMessage* message; ~Release() { message->Release(); } } owner{created};
        auto& sfx=*static_cast<LMessageImpl*>(created);
        sfx.SetObjectIO(client_message_io(scene_,resources_,engine_));
        if (sfx.WriteRaw(effect.data(),uint32(effect.size()))!=LT_OK) throw std::bad_alloc();
        sfx.ResetPos();
        events_.special_effect(object,sfx);
        // The effect may have added the trackers these records describe.
        if (type==OT_MODEL && !trackers.empty()) apply(object,trackers,true);
    }
}
// One record per server tracker, applied in list order to the trackers the
// client game added for them (the main tracker first). Unguaranteed updates
// leave a playing animation's own clock alone.
std::vector<ClientWorldSync::TrackerRecord> ClientWorldSync::animation(LMessageImpl& message,HOBJECT object,bool always) {
    Reader input{message};
    std::vector<TrackerRecord> records(input.u8());
    for (std::size_t i=0;i<records.size();++i) {
        records[i].word=input.u16(); records[i].percent=input.u8();
        records[i].weight=i ? input.u8() : 0xffffffffu;
    }
    apply(object,records,always);
    return records;
}
void ClientWorldSync::apply(HOBJECT object,const std::vector<TrackerRecord>& records,bool always) {
    if (!object || object->m_ObjectType!=OT_MODEL || !scene_->contains(object)) return;
    auto& model=scene_->model();
    auto* tracker=static_cast<ModelInstance*>(object)->m_pTrackerList;
    for (const auto& record : records) {
        if (!tracker) break;
        auto* next=tracker->GetNext();
        const auto anim=HMODELANIM(record.word&0x3fff); const bool playing=record.word&0x4000;
        model.SetLooping(tracker,(record.word&0x8000) ? LTTRUE : LTFALSE);
        model.SetPlaying(tracker,playing ? LTTRUE : LTFALSE);
        HMODELANIM current=0;
        const bool unchanged=playing && !always && model.GetCurAnim(tracker,current)==LT_OK && current==anim;
        if (!unchanged && model.SetCurAnim(tracker,anim)==LT_OK) {
            uint32 length=0; model.GetCurAnimLength(tracker,length);
            model.SetCurAnimTime(tracker,std::uint32_t(std::uint64_t(std::max<uint32>(length,1))*record.percent/255));
        }
        model.SetWeightSet(tracker,record.weight);
        tracker=next;
    }
}
void ClientWorldSync::position(HOBJECT object,LTVector place,bool teleport) {
    if (events_.move) events_.move(object,teleport,place);
    if (scene_->contains(object)) scene_->position(object,place,teleport);
}
void ClientWorldSync::rotation(HOBJECT object,LTRotation value,bool teleport) {
    if (events_.rotate) events_.rotate(object,teleport,value);
    if (scene_->contains(object)) scene_->rotation(object,value);
}
void ClientWorldSync::unguaranteed_packet(LMessageImpl& message) {
    Reader input{message};
    while (input.remaining()) {
        const auto word=input.u16();
        if (word==0xffff) { server_time_=input.f32(); continue; }
        const auto id=std::uint16_t(word&0xfff); const auto object=this->object(id);
        if (word&0x4000) { const auto place=input.compressed_position(); const auto velocity=input.compressed_vector(); if (object) { object->m_Velocity=velocity; position(object,place,false); } }
        if (word&0x8000) { const auto turn=yaw(input.u8()); if (object) rotation(object,turn,false); }
        else if (word&0x2000) { const auto turn=input.compressed_rotation(); if (object) rotation(object,turn,false); }
        if (word&0x1000) animation(message,object,false);
    }
}
void ClientWorldSync::sound(LMessageImpl& message,const ServerFiles& files) {
    Reader input{message};
    const auto file=input.u16(); auto& state=sounds_[file];
    const auto change=input.u8();
    if (change&1) state.flags=input.u16();
    if (change&2) state.priority=input.u8();
    PlaySoundInfo info; PLAYSOUNDINFO_INIT(info);
    info.m_dwFlags=state.flags|PLAYSOUND_CLIENT; info.m_nPriority=state.priority;
    const bool spatial=info.m_dwFlags&(PLAYSOUND_AMBIENT|PLAYSOUND_3D),local=info.m_dwFlags&PLAYSOUND_CLIENTLOCAL;
    if (spatial) {
        if (change&4) { state.range=input.u16(); state.inner=input.u8(); }
        info.m_fOuterRadius=state.range; info.m_fInnerRadius=info.m_fOuterRadius*state.inner/255.f;
    }
    info.m_nVolume=(info.m_dwFlags&PLAYSOUND_CTRL_VOL) ? input.u8() : 100;
    info.m_fPitchShift=(info.m_dwFlags&PLAYSOUND_CTRL_PITCH) ? input.f32() : 1.f;
    if (spatial && !local) info.m_vPosition=input.compressed_position();
    info.m_UserData=input.u32();
    const auto* found=files.find(file);
    if (found && events_.sound) events_.sound(info,found->name,0);
}
void ClientWorldSync::tracked_sound(LMessageImpl& message,std::uint16_t id,std::uint16_t change,const ServerFiles& files) {
    Reader input{message};
    PlaySoundInfo info; PLAYSOUNDINFO_INIT(info); float offset=0; const ServerFile* found=nullptr;
    if (change&new_object) {
        const auto file=input.u16(); found=files.find(file); auto& state=sounds_[file];
        const auto flags=input.u8();
        if (flags&1) state.flags=input.u16();
        info.m_dwFlags=state.flags;
        if (flags&2) state.priority=input.u8();
        info.m_nPriority=state.priority;
        if (info.m_dwFlags&(PLAYSOUND_AMBIENT|PLAYSOUND_3D)) {
            if (flags&4) { state.range=input.u16(); state.inner=input.u8(); }
            info.m_fOuterRadius=state.range; info.m_fInnerRadius=info.m_fOuterRadius*state.inner/255.f;
        }
        info.m_nVolume=(info.m_dwFlags&PLAYSOUND_CTRL_VOL) ? input.u8() : 100;
        info.m_fPitchShift=(info.m_dwFlags&PLAYSOUND_CTRL_PITCH) ? input.f32() : 1.f;
        if (info.m_dwFlags&PLAYSOUND_TIMESYNC) { std::uint32_t at=input.u8(); if (at==0xff) at=input.u32(); offset=at*0.001f; }
        info.m_UserData=input.u32();
    }
    if (change&position_changed) info.m_vPosition=input.compressed_position();
    if (change&new_object) {
        stop(id);
        info.m_dwFlags|=PLAYSOUND_GETHANDLE;
        if (found && events_.sound) if (const auto handle=events_.sound(info,found->name,offset)) tracked_[id]=handle;
    } else if (change&position_changed) {
        if (const auto it=tracked_.find(id); it!=tracked_.end() && events_.move_sound) events_.move_sound(it->second,info.m_vPosition);
    }
}
void ClientWorldSync::stop(std::uint16_t id) {
    const auto it=tracked_.find(id); if (it==tracked_.end()) return;
    const auto handle=it->second; tracked_.erase(it);
    if (events_.stop_sound) events_.stop_sound(handle);
}
void ClientWorldSync::removed(LMessageImpl& message) {
    Reader input{message};
    while (input.remaining()) {
        const auto id=input.u16();
        stop(id);
        if (const auto found=objects_.find(id); found!=objects_.end()) {
            const auto object=found->second.object; objects_.erase(found);
            if (scene_->contains(object)) scene_->remove(object);
        }
    }
}
void ClientWorldSync::light_animations(LMessageImpl& message) {
    Reader input{message};
    while (input.remaining()) {
        const auto handle=input.u16(); const auto change=input.u8();
        LAInfo info; const bool known=scene_->lighting().GetLightAnimInfo(HLIGHTANIM(handle),info)==LT_OK;
        if (change&1) { info.m_iFrames[0]=input.u16(); info.m_iFrames[1]=input.u16(); }
        if (change&2) info.m_fPercentBetween=input.u8()/255.f;
        if (change&4) info.m_fBlendPercent=input.u8()/255.f;
        if (change&8) info.m_vLightPos=input.compressed_position();
        if (change&0x10) { info.m_vLightColor.x=input.u8(); info.m_vLightColor.y=input.u8(); info.m_vLightColor.z=input.u8(); }
        if (change&0x20) info.m_fLightRadius=input.f32();
        if (known) scene_->lighting().SetLightAnimInfo(HLIGHTANIM(handle),info);
    }
}
void ClientWorldSync::filenames(LMessageImpl& message,const ServerFiles& files) {
    Reader input{message};
    const auto id=input.u16(); ObjectCreateStruct create;
    const auto file=[&](std::uint16_t file_id,char* output,std::size_t capacity) {
        if (file_id==0xffff) return;
        const auto* found=files.find(file_id); if (!found || found->name.size()>=capacity) invalid("unknown file ID");
        std::memcpy(output,found->name.c_str(),found->name.size()+1);
    };
    file(input.u16(),create.m_Filename,sizeof create.m_Filename);
    for (auto& skin : create.m_SkinNames) file(input.u16(),skin,sizeof skin);
    if (const auto target=object(id)) scene_->filenames(target,create);
}
}
