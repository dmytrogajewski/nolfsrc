#pragma once
#include <cstdint>
// Original game controller binding layout.
struct P7PadState
{
	std::uint16_t	wButtons;
	std::uint8_t	bLeftTrigger;
	std::uint8_t	bRightTrigger;
	std::int16_t	sThumbLX;
	std::int16_t	sThumbLY;
	std::int16_t	sThumbRX;
	std::int16_t	sThumbRY;
};

// Stable game controller button bits
enum
{
	P7PAD_DPAD_UP			= 0x0001,
	P7PAD_DPAD_DOWN			= 0x0002,
	P7PAD_DPAD_LEFT			= 0x0004,
	P7PAD_DPAD_RIGHT		= 0x0008,
	P7PAD_START				= 0x0010,
	P7PAD_BACK				= 0x0020,
	P7PAD_LEFT_THUMB		= 0x0040,
	P7PAD_RIGHT_THUMB		= 0x0080,
	P7PAD_LEFT_SHOULDER		= 0x0100,
	P7PAD_RIGHT_SHOULDER	= 0x0200,
	P7PAD_A					= 0x1000,
	P7PAD_B					= 0x2000,
	P7PAD_X					= 0x4000,
	P7PAD_Y					= 0x8000,
	P7PAD_TRIGGER_THRESHOLD	= 30		// XINPUT_GAMEPAD_TRIGGER_THRESHOLD
};


#ifdef NOLFSRC_NATIVE
#include "engine/client/client_window.h"
#include "platform/window.h"
#include <algorithm>
#include <cmath>
inline bool P7PadRead_Available() { return g_pLTClient != nullptr; }
inline bool P7PadRead_IsTest() { return false; }
inline bool P7PadRead_State(P7PadState* output) {
    if (!output) return false;
    *output={};
    if (!g_pLTClient) return false;
    const auto state=nolf::client_window(*g_pLTClient).gamepad_state();
    if (!state) return false;
    auto thumb=[](float value) { return static_cast<std::int16_t>(std::lround(std::clamp(value,-1.0f,1.0f)*32767.0f)); };
    auto trigger=[](float value) { return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0f,1.0f)*255.0f)); };
    output->wButtons=state->buttons;
    output->sThumbLX=thumb(state->left_x); output->sThumbLY=thumb(state->left_y);
    output->sThumbRX=thumb(state->right_x); output->sThumbRY=thumb(state->right_y);
    output->bLeftTrigger=trigger(state->left_trigger); output->bRightTrigger=trigger(state->right_trigger);
    return true;
}
#else
#include "platform/windows/legacy_gamepad.h"
#endif
