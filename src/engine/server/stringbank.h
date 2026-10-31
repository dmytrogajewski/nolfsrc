#pragma once
#include "ltbasedefs.h"
#include <optional>
#include <string>

HSTRING sb_CopyString(HSTRING);
void sb_ReleaseString(HSTRING);
void sb_FreeString(HSTRING);
LTBOOL sb_CompareStrings(HSTRING, HSTRING);
LTBOOL sb_CompareStringsUpper(HSTRING, HSTRING);
char* sb_GetStringData(HSTRING);
char* sb_GetStringDataAndLen(HSTRING, uint32*);
std::optional<std::string> sb_GetStringSnapshot(HSTRING);
HSTRING sb_CreateString(char*);
HSTRING sb_CreateStringRaw(char*);
void sb_EnumStrings(void (*)(char*, void*), void*);
void sb_EnumThreadStrings(void (*)(char*, void*), void*);
extern char g_EmptyString[];
