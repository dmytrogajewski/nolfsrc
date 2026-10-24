#pragma once
#include "ltbasedefs.h"
#include <string>

struct SoundRecord {
    std::string file_name;
    uint16 file_id = 0;
    PlaySoundInfo m_Info{};
};
