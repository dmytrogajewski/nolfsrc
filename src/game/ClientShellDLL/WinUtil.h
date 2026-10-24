#ifndef __WINUTIL_H
#define __WINUTIL_H
#include "ltbasetypes.h"
// Legacy game-facing facade; writable files and profile documents are native services.
class CWinUtil {
public:
    static LTBOOL DirExist(const char* path);
    static LTBOOL CreateDir(const char* path);
    static LTBOOL FileExist(const char* path);
    static uint32 WinGetPrivateProfileString(const char* section,const char* key,const char* fallback,char* output,uint32 capacity,const char* file);
    static LTBOOL WinWritePrivateProfileString(const char* section,const char* key,const char* value,const char* file);
    static void DebugOut(const char*);
    static float GetTime();
};
#endif
