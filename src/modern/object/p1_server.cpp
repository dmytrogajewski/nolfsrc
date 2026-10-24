// Multiplayer server cadence, shared by listen and headless servers.
#include "Stdafx.h"
#include "object/p1_server.h"
#include "platform/timing.h"

namespace {
nolf::platform::FrameLimiter limiter(60);
bool locked = true;
}
void P1Server_Init() { locked = true; limiter.reset(); }
void P1Server_OnDontLockMessage(HCLIENT sender, HMESSAGEREAD message) {
    const auto unlock = g_pLTServer->ReadFromMessageByte(message);
    if (sender && (g_pLTServer->GetClientInfoFlags(sender) & CIF_LOCAL) && unlock) {
        locked = false;
        limiter.reset();
    }
}
void P1Server_EndUpdate(bool multiplayer) {
    if (multiplayer && locked) limiter.wait();
    else limiter.reset();
}
