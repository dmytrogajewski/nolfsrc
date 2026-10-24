// The native clock and frame deadline use the consolidated timing adapter.
#include "Stdafx.h"
#include "MsgIDs.h"
#include "cshell/p1_modern.h"
#include "platform/timing.h"
#include <algorithm>
#include <optional>

namespace {
const auto origin=nolf::platform::Clock::now();
bool suspended=false;
unsigned current_rate=0;
std::optional<nolf::platform::FrameLimiter> limiter;
}
float P1Timing_GetTime()
{
    return std::chrono::duration<float>(nolf::platform::Clock::now()-origin).count();
}
void P1Timing_SuspendFrameLimit(bool suspend)
{
    suspended=suspend;
    if (limiter) limiter->reset();
}
void P1Timing_EndFrame()
{
    const auto rate=unsigned(std::clamp(P1Config_FramerateLimit(),0,1000));
    if (suspended || !rate) { limiter.reset(); current_rate=0; return; }
    if (!limiter || rate != current_rate) { limiter.emplace(rate); current_rate=rate; }
    limiter->wait();
}

void P1Timing_SendDontLockServer(bool bHosting)
{
	if (!bHosting)
		return;

	// M-P1-02: the hosting client and its server share one loop, which the
	// client's limiter already paces.  (A retail server ignores the
	// message.)
	HMESSAGEWRITE hWrite = g_pLTClient->StartMessage(MID_DONT_LOCK_SERVER_FPS);
	g_pLTClient->WriteToMessageByte(hWrite, 1);
	g_pLTClient->EndMessage(hWrite);
}
