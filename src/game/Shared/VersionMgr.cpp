// ----------------------------------------------------------------------- //
//
// MODULE  : VersionMgr.h
//
// PURPOSE : Definition of versioning manager
//
// CREATED : 11/16/2000
//
// (c) 2000 Monolith Productions, Inc.  All Rights Reserved
//
// ----------------------------------------------------------------------- //

#include "Stdafx.h"
#include "engine/portable/string.h"
#include "VersionMgr.h"

#include "platform/settings.h"
#include "platform/system.h"

#ifdef _DEMO
static const char* s_szVersion		= "";
static const char* s_szBuild		= "Demo Build 7";
#else
// GOTY: the Game of the Year CShell.dll and Object.lto say "v1.004" and
// "Build 415" (the strings beside "German"/"Language" in both); the v1.003
// source release says "v1.003", "Build 410"
static const char* s_szVersion		= "v1.004";
static const char* s_szBuild		= "Build 415";
#endif 
static const uint32 s_nSaveVersion  = 4;

CVersionMgr* g_pVersionMgr = NULL;

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CVersionMgr::CVersionMgr()
//
//	PURPOSE:	Constructor
//
// ----------------------------------------------------------------------- //

CVersionMgr::CVersionMgr()
{
	g_pVersionMgr = this;

	// Determine what language version the game is using...

	m_szLanguage[0]	= '\0';
	m_bLowViolence	= false;

    std::string language = "English";
    try { language = nolf::platform::Settings().get("language", "English"); }
    catch (const std::exception& error) { nolf::platform::log("preferences", error.what()); }
    std::snprintf(m_szLanguage, sizeof(m_szLanguage), "%s", language.c_str());
    m_bLowViolence = nolf::ascii_compare(m_szLanguage, "German") == 0;

}

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CVersionMgr::GetVersion()
//
//	PURPOSE:	Get the current version string
//
// ----------------------------------------------------------------------- //

const char* CVersionMgr::GetVersion()
{
	return s_szVersion;
}

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CVersionMgr::GetSaveVersion()
//
//	PURPOSE:	Get the current save version 
//
// ----------------------------------------------------------------------- //

const uint32 CVersionMgr::GetSaveVersion()
{
	return s_nSaveVersion;
}

// ----------------------------------------------------------------------- //
//
//	ROUTINE:	CVersionMgr::GetBuild()
//
//	PURPOSE:	Get the current build string
//
// ----------------------------------------------------------------------- //

const char* CVersionMgr::GetBuild()
{
	return s_szBuild;
}