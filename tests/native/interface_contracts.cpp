#include "engine/server/servermsg.h"
#include "engine/server/servermgr.h"
#include <type_traits>

// Every native build checks that real SDK inheritance implements every slot.
// Historical generated vtables could hide missing and incompatible methods.
static_assert(std::is_base_of_v<ILTServer, CLTServer>);
static_assert(!std::is_abstract_v<CLTServer>);
static_assert(std::is_base_of_v<ILTModel, ServerModelLT>);
static_assert(!std::is_abstract_v<ServerModelLT>);
static_assert(std::is_base_of_v<CMsgObjectIO, CMessageObjectHelper>);
static_assert(!std::is_abstract_v<CMessageObjectHelper>);
static_assert(std::is_base_of_v<ILTMessage, LMessageImpl>);
static_assert(!std::is_abstract_v<LMessageImpl>);

static_assert(std::is_abstract_v<ILTModel>);
static_assert(std::is_abstract_v<ILTPhysics>);
static_assert(std::is_base_of_v<ILTPhysics, SPhysicsLT>);
static_assert(!std::is_abstract_v<SPhysicsLT>);
static_assert(sizeof(HLIGHTANIM) == 4);

static_assert(std::is_base_of_v<ILTSoundMgr, CServerMgr>);
static_assert(!std::is_abstract_v<CServerMgr>);
