#pragma once
#include "message.h"
#include "object_lifetime.h"
#include <unordered_map>

class CUpdateContext {
public:
    class CServerMgr* m_pMgr = nullptr;
    class Client* m_pClient = nullptr;
    std::shared_ptr<const bool> m_ClientLifetime;
    CMessageRef m_Msg1, m_Msg2;
    CMsgCursor m_Cursors[2];
    uint32 m_Unknown18 = 0, m_bLocal = 0;
    void MarkVisible(LTObject& object) { visible_.insert_or_assign(&object,ObjectCallbackGuard(object)); }
    bool IsVisible(LTObject& object) const {
        const auto found = visible_.find(&object);
        return found != visible_.end() && found->second.unchanged();
    }
    bool ClientAlive() const { return m_ClientLifetime && *m_ClientLifetime; }
private:
    std::unordered_map<LTObject*,ObjectCallbackGuard> visible_;
};
