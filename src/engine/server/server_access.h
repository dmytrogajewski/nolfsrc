#pragma once
#include "server_lifetime.h"
#include "platform/sync.h"

class CServerMgr;
// Serializes public access with CreateServer/DeleteServer. Recursive calls on
// the engine thread can request retirement without deleting an active owner.
class ServerInterfaceCall {
public:
    ServerInterfaceCall();
    CServerMgr* manager() const { return manager_; }
    bool available() const { return activity_.available(); }
private:
    std::unique_lock<nolf::platform::RecursiveMutex> lock_;
    CServerMgr* manager_;
    ServerActivity activity_;
};
