#pragma once
#include "ltbasetypes.h"
#include "ltcodes.h"
class Model;
class ILTStream;
class CServerMgr;
class CModelLoadRequest;
LTRESULT se_NoChildModel(CModelLoadRequest*,Model**);
struct CModelLoadContext {
    CServerMgr* m_pServerMgr = nullptr;
    char* m_pFilename = nullptr;
};
struct CModelLoadRequest {
    ILTStream* m_pStream = nullptr;
    LTRESULT (*m_pfnLoadChild)(CModelLoadRequest*,Model**) = se_NoChildModel;
    CModelLoadContext* m_pContext = nullptr;
    char* m_pChildName = nullptr;
    void* m_UserData = nullptr;
    LTBOOL m_bUnknown10 = LTTRUE;
    LTBOOL m_bTreesValid = LTTRUE;
    LTBOOL m_bUnknown18 = LTTRUE;
    LTBOOL m_bChildrenFound = LTTRUE;
};
