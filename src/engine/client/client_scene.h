#pragma once
#include "engine/portable/assets.h"
#include "engine/portable/sky.h"
#include "engine/server/object_lifetime.h"
#include "engine/server/world_move.h"
#include "engine/server/world_polygon_geometry.h"
#include "iltclient.h"
#include <array>
#include <functional>
#include <optional>

class CServerWorld;
namespace nolf {
class SpriteResource;
class ClientEffects;
struct SceneFunctions {
#define NOLF_SCENE_API(name) decltype(ILTClient::name) name=nullptr;
#include "client_scene_members.inc"
#undef NOLF_SCENE_API
};
struct ClientSceneEvents {
    // Hosts retain dynamically loaded callback code with this lease. It is
    // declared first so callback closures are destroyed before their code.
    std::shared_ptr<const void> code;
    std::function<void(HOBJECT)> removed;
    std::function<void(HOBJECT,const CollisionInfo&,float)> touch;
    std::function<void(HOBJECT,ArgList&)> model_key;
};
enum class ClientObjectLifetime { Client, World };
struct ClientObjectInfo {
    std::string name,filename;
    std::array<std::string,MAX_MODEL_TEXTURES> skins;
    ClientObjectLifetime lifetime=ClientObjectLifetime::Client;
    std::optional<uint16> server_id;
    uint32 client_flags=0,dirty=0;
    void* user=nullptr;
    bool removing=false,main_world=false;
};
struct ClientCamera {
    bool fullscreen=true;
    LTRect rectangle{0,0,640,480};
    float fov_x=1.5707964f,fov_y=1.5707964f;
    LTVector light_add{0,0,0};
};
struct ClientCanvas { CanvasDrawFn draw=nullptr; void* user=nullptr; float radius=1; };
// Owns canonical engine objects, their metadata and resources. SDK facades
// borrow this state; callback scopes keep it alive through host retirement.
class ClientScene {
    struct State;
public:
    explicit ClientScene(std::shared_ptr<const Assets> = {});
    ~ClientScene();
    ClientScene(const ClientScene&)=delete;
    ClientScene& operator=(const ClientScene&)=delete;
    class Scope {
    public:
        explicit Scope(const ClientScene&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        std::shared_ptr<State> previous_,current_;
        std::shared_ptr<const void> code_;
    };
    Scope activate() const { return Scope(*this); }
    static const SceneFunctions& functions();
    void install(ILTClient&) const;
    std::shared_ptr<const void> retain() const;
    void events(ClientSceneEvents);
    bool contains(HOBJECT) const;
    std::size_t size() const;
    std::optional<ClientObjectInfo> info(HOBJECT) const;
    std::shared_ptr<const SpriteResource> sprite(HOBJECT) const;
    LTRESULT sprite_control(HOBJECT,ILTSpriteControl*&);
    std::vector<MoveObjectReference> snapshot() const;
    LTRESULT create(const ObjectCreateStruct&,HOBJECT&,ClientObjectLifetime = ClientObjectLifetime::Client);
    LTRESULT remove(HOBJECT);
    LTRESULT clear();
    LTRESULT filenames(HOBJECT,const ObjectCreateStruct&);
    LTRESULT position(HOBJECT,LTVector,bool force=false);
    LTRESULT rotation(HOBJECT,LTRotation);
    LTRESULT scale(HOBJECT,LTVector);
    LTRESULT color(HOBJECT,float r,float g,float b,float a);
    LTRESULT client_flags(HOBJECT,uint32);
    LTRESULT user_data(HOBJECT,void*);
    LTRESULT server_id(HOBJECT,std::optional<uint16>);
    std::optional<uint16> server_id(HOBJECT) const;
    HOBJECT find_server_object(uint16) const;
    LTRESULT camera(HOBJECT,const ClientCamera&);
    std::optional<ClientCamera> camera(HOBJECT) const;
    LTRESULT viewport(uint32 width,uint32 height);
    LTRESULT attach(HOBJECT parent,HOBJECT child,int socket,LTVector,LTRotation,HATTACHMENT&);
    LTRESULT detach(HATTACHMENT);
    LTRESULT process_attachments(HOBJECT);
    LTRESULT node_control(HOBJECT,NodeControlFn,void*);
    std::vector<HOBJECT> containers(HOBJECT) const;
    uint32 point_containers(LTVector,HOBJECT*,uint32) const;
    LTRESULT load_world(std::string_view);
    LTRESULT unload_world();
    std::string world_name() const;
    HOBJECT main_world() const;
    std::shared_ptr<const CServerWorld> world_data() const;
    SkyInfo sky() const;
    LTRESULT sky(const SkyInfo&);
    std::vector<MoveObjectReference> sky_objects() const;
    std::optional<WorldPolygonGeometry> polygon_geometry(HPOLY,LTVector near_point) const;
    std::vector<MoveObjectReference> visible(LTVector position,float radius) const;
    LTRESULT update(float elapsed);
    void free_unused_models();
    ILTClientPhysics& physics();
    ILTModel& model();
    ILTCommon& common();
    ILTMath& math();
    ILTTransform& transform();
    ILTLightAnim& lighting();
    ClientEffects& effects();
    std::optional<ClientCanvas> canvas(HOBJECT) const;
    LTRESULT canvas_function(HOBJECT,CanvasDrawFn,void*);
    LTRESULT canvas_radius(HOBJECT,float);
private:
    std::shared_ptr<State> state_;
};
}
