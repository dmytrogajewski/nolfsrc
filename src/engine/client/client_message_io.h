#pragma once
#include <memory>
#include <cstdint>
class CMsgObjectIO;
namespace nolf {
class ClientScene;
class Resources;
// A retained packet owns the codec context, which borrows its engine/scene
// weakly so it cannot keep a retired game alive or dereference freed state.
// Object/position codecs are bound to the level present at creation.
std::shared_ptr<CMsgObjectIO> client_message_io(std::weak_ptr<ClientScene>,
    std::shared_ptr<const Resources>,std::weak_ptr<const void> engine_owner,
    std::uint32_t language=1033);
}
