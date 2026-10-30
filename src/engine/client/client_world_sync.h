#pragma once
#include "engine/client/client_scene.h"
#include "engine/client/server_files.h"
#include "engine/portable/resources.h"
#include <functional>
#include <map>

namespace nolf {
// Game callbacks for server-driven changes, in the order the engine applies
// them. Hosts retain their game module with `code`.
struct ClientSyncEvents {
    std::shared_ptr<const void> code;
    // A new object's or an instant effect's message (object null).
    std::function<void(HOBJECT,ILTMessage&)> special_effect;
    // Before a server move or rotation is applied; the game may adjust it.
    std::function<void(HOBJECT,bool teleport,LTVector&)> move;
    std::function<void(HOBJECT,bool teleport,LTRotation&)> rotate;
    // Plays a server sound; a tracked one (PLAYSOUND_GETHANDLE) answers its
    // handle, which the sync later moves and stops when the server removes it.
    std::function<HLTSOUND(PlaySoundInfo&,const std::string& file,float offset)> sound;
    std::function<void(HLTSOUND,const LTVector&)> move_sound;
    std::function<void(HLTSOUND)> stop_sound;
    // The world's global light (packet 26): the sun's direction and colour
    // and the light scale, which the original hands to SetGlobalLightDir,
    // SetGlobalLightColor and SetGlobalLightScale.
    std::function<void(LTVector direction,LTVector color,float scale)> global_light;
};
// Applies the engine's object, sound and lighting update packets (8, 10,
// 15, 18, 25, 26) to the client scene, exactly as the original client read
// them. Server objects live for the current world only.
class ClientWorldSync {
public:
    ClientWorldSync(std::shared_ptr<ClientScene>,std::shared_ptr<const Resources>,std::weak_ptr<const void> engine,ClientSyncEvents);
    ~ClientWorldSync();
    ClientWorldSync(const ClientWorldSync&)=delete;
    ClientWorldSync& operator=(const ClientWorldSync&)=delete;
    // Forgets server objects; the scene clears them with the world.
    void reset();
    void apply(std::uint8_t packet,std::span<const std::byte>,const ServerFiles&);
    HOBJECT object(std::uint16_t server_id) const;
    std::size_t size() const { return objects_.size(); }
    float server_time() const { return server_time_; }
    // Server objects the scene removed itself (the game removed them).
    void forget(HOBJECT);
private:
    struct Object { HOBJECT object=nullptr; std::uint8_t type=0; };
    struct SoundState { std::uint16_t flags=0,range=0; std::uint8_t priority=0,inner=0; };
    LMessageImpl& message(std::span<const std::byte>);
    void update_packet(LMessageImpl&,const ServerFiles&);
    void unguaranteed_packet(LMessageImpl&);
    void object_record(LMessageImpl&,std::uint16_t flags,const ServerFiles&);
    struct TrackerRecord { std::uint16_t word=0; std::uint8_t percent=0; std::uint32_t weight=0; };
    std::vector<TrackerRecord> animation(LMessageImpl&,HOBJECT,bool always);
    void apply(HOBJECT,const std::vector<TrackerRecord>&,bool always);
    void position(HOBJECT,LTVector,bool teleport);
    void rotation(HOBJECT,LTRotation,bool teleport);
    void sound(LMessageImpl&,const ServerFiles&);
    void tracked_sound(LMessageImpl&,std::uint16_t id,std::uint16_t flags,const ServerFiles&);
    void removed(LMessageImpl&);
    void light_animations(LMessageImpl&);
    void filenames(LMessageImpl&,const ServerFiles&);
    struct PendingAttachment { std::uint16_t parent=0,child=0; std::uint32_t node=0; LTVector offset; LTRotation turn; };
    std::vector<PendingAttachment> pending_attachments_;
    void attach_pending(std::uint16_t child);
    // Attachments made from server records, by parent, which the parent's
    // next list replaces.
    struct ServerAttachment {
        HATTACHMENT handle=nullptr;
        uint64 generation=0;
        PendingAttachment source;
    };
    std::map<std::uint16_t,std::vector<ServerAttachment>> server_attachments_;
    void retire_attachments(std::uint16_t id,bool refresh);
    void attach(std::uint16_t id,HOBJECT parent,HOBJECT child,std::uint32_t node,LTVector offset,LTRotation turn);
    std::shared_ptr<ClientScene> scene_;
    std::shared_ptr<const Resources> resources_;
    std::weak_ptr<const void> engine_;
    ClientSyncEvents events_;
    std::map<std::uint16_t,Object> objects_;
    std::map<std::uint16_t,SoundState> sounds_;
    // Tracked server sounds by ID, until the server removes them.
    std::map<std::uint16_t,HLTSOUND> tracked_;
    void stop(std::uint16_t id);
    ILTMessage* reader_=nullptr;
    float server_time_=0;
};
}
