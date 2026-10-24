#include "engine/client/texture_link.h"
#include "engine/portable/command_reader.h"
#include "engine/portable/string.h"

namespace nolf {
TextureLink texture_link(const TextureHeader& header) {
    CommandReader reader(header.commands); TextureLink found;
    while (auto statement=reader.next()) {
        if (statement->size()<2) continue;
        const auto& command=(*statement)[0];
        if (ascii_compare(command,"DetailTex")==0) return {TextureLinkType::Detail,(*statement)[1]};
        if (ascii_compare(command,"EnvMap")==0) found={TextureLinkType::Environment,(*statement)[1]};
        else if (ascii_compare(command,"EnvMapAlpha")==0) found={TextureLinkType::EnvironmentAlpha,(*statement)[1]};
    }
    return found;
}
}
