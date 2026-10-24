#pragma once
#include "ltbasedefs.h"
#include "iltstream.h"

class CServerMgr;
struct ObjectList;
LTRESULT sav_SaveObjects(CServerMgr*, ILTStream*, ObjectList*, uint32 parameter, uint32 flags);
LTRESULT sav_RestoreObjects(CServerMgr*, ILTStream*, uint32 parameter, uint32 flags);
LTRESULT sav_SaveFile(CServerMgr*, const char*, ObjectList*, uint32 parameter, uint32 flags);
LTRESULT sav_RestoreFile(CServerMgr*, const char*, uint32 parameter, uint32 flags);
