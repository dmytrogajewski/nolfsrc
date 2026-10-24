#pragma once
#include "client_state.h"
#include "sound_instance.h"
#include "position_reference.h"

// Append a complete update record before committing client delta state or
// listener holds. These use fixed-width Windows wire fields only.
LTRESULT sound_WriteQueued(const SoundRecord&, Client&, ServerMessage&, CPosRef*);
LTRESULT sound_WriteInstance(SoundInstance&, CObjInfo&, Client&, ServerMessage&, CPosRef*, float server_time);
