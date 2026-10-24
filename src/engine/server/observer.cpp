#include "observer.h"

CModelSub14::~CModelSub14() { sub_10024419(); }
LTBOOL CModelSub14::Init(void* owner) {
    sub_10024419();
    m_pOwner = owner;
    return LTTRUE;
}
void CModelSub14::sub_10024419() {
    uint32 destroying = 1;
    Walk(nullptr, &destroying);
    m_pHead = nullptr;
}
LTBOOL CModelSub14::Walk(void* first, void* second) {
    for (auto* node = m_pHead; node;) {
        auto* next_node = node->m_pNext;
        for (auto* hook = node->m_pHooks; hook;) {
            auto* next_hook = hook->m_pNext;
            if (hook->m_pFn) hook->m_pFn(this, node, first, second);
            hook = next_hook;
        }
        node = next_node;
    }
    return LTFALSE;
}
