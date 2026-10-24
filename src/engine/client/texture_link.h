#pragma once
#include "engine/portable/texture.h"
#include <string>

namespace nolf {
// The texture a DTX names in its command string (r_LoadSystemTexture):
// DetailTex (detail) wins, else EnvMap (environment), which EnvMapAlpha
// replaces.
enum class TextureLinkType { None=-1,Detail=0,Environment=1,EnvironmentAlpha=2 };
struct TextureLink { TextureLinkType type=TextureLinkType::None; std::string name; };
TextureLink texture_link(const TextureHeader&);
}
