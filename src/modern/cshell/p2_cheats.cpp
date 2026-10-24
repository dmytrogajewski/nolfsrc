// ----------------------------------------------------------------------- //
//
// MODULE  : p2_cheats.cpp
//
// PURPOSE : M-P2-09, the "Cheats" console program (NOLFSRC_MODERN only)
//
// ----------------------------------------------------------------------- //
//
// Lists the cheat codes the console (and the chat line) accepts, with what
// each does. The codes are the game's own (MessageMgr.cpp's CCheatMgr keeps
// them obfuscated); the descriptions are ours. The six marked "(modern)"
// are compiled out of the retail final build and back in this one
// (MessageMgr.cpp, CCheatMgr::Process).

#include "Stdafx.h"

namespace
{
	struct P2CheatDesc
	{
		const char* pCode;
		const char* pWhat;
	};

	const P2CheatDesc s_Cheats[] =
	{
		{ "mpimyourfather",              "god mode on/off" },
		{ "mpdrdentz",                   "full health" },
		{ "mpwonderbra",                 "full armor" },
		{ "mpwegotdeathstar",            "full ammunition" },
		{ "mpmimimi",                    "every weapon" },
		{ "mpgoattech",                  "every modification for the weapons carried" },
		{ "mpyoulooklikeyouneedamonkey", "every gadget" },
		{ "mpsanta",                     "weapons, ammunition, armor and health" },
		{ "mpkingoftehmonstars",         "every weapon, ammunition never runs out" },
		{ "mphookmeup",                  "the mission's starting equipment (modern)" },
		{ "mpclip",                      "fly through walls on/off -- also the way out of a stuck spot (modern)" },
		{ "mpteleport",                  "back to the level's start point (modern)" },
		{ "mpboyisuck",                  "remove every AI from the level on/off (modern)" },
		{ "mpbeenthere",                 "every mission selectable on/off (modern)" },
		{ "mpwhoami",                    "clear the player's record (modern)" },
		{ "mpmaphole",                   "finish the current level" },
		{ "mpracerboy",                  "a motorcycle" },
		{ "mprosebud",                   "a snowmobile" },
		{ "mpasscam",                    "first / third person view" },
		{ "mppos",                       "show the player's position on/off" },
		{ "mpexorbitantamounts",         "much more blood on/off" },
		{ "mpbuild",                     "show the build's version" },
	};
}

void P2Cheats_ListFn(int argc, char** argv)
{
	int i;

	g_pLTClient->CPrint("Cheat codes (single player; type one here or on the chat line):");

	for (i = 0; i < (int)(sizeof(s_Cheats) / sizeof(s_Cheats[0])); i++)
	{
		g_pLTClient->CPrint("  %-28s %s", s_Cheats[i].pCode, s_Cheats[i].pWhat);
	}
}
