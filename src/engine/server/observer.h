#pragma once
#include "ltbasetypes.h"

class CModelSub14;
struct ObserverNode;
struct ObserverHook {
    using Callback = void (*)(CModelSub14*, ObserverNode*, void*, void*);
    Callback m_pFn = nullptr;
    ObserverHook* m_pNext = nullptr;
};
struct ObserverNode {
    ObserverHook* m_pHooks = nullptr;
    void* m_pData = nullptr;
    ObserverNode* m_pNext = nullptr;
};

// Models and world surfaces share the same owner/notification chain.
class CModelSub14 {
public:
    CModelSub14() = default;
    ~CModelSub14();
    CModelSub14(const CModelSub14&) = delete;
    CModelSub14& operator=(const CModelSub14&) = delete;
    LTBOOL Init(void* owner);
    void sub_10024419();
    LTBOOL Walk(void* first, void* second);
    ObserverNode* m_pHead = nullptr;
    void* m_pOwner = nullptr;
};
