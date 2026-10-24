#include "net_connection.h"
#include "net_driver.h"
#include "net_packet.h"
#include <cassert>
#include <cmath>
extern float g_AckTimeout;
extern int g_NetShowTimeouts;
void dsi_ServerError(char*, ...);

CRateTracker::CRateTracker()
{
	m_Time = 0.0001f;
	m_Unknown08 = 2.0f;
	m_Value = 0.0f;
}

NetConnection::NetConnection() : m_Counter(0)
{
    m_pDriver = nullptr;
    m_Unknown64 = m_Unknown68 = m_Unknown6C = m_Flags = 0;
    m_RecvWait = m_SendWait = m_AckTimer = m_Unknown98 = m_Ping = m_UnknownB4 = 0;
}

LTBOOL NetConnection::CheckAckTimeout()
{
	if (m_Flags & 1)
		return LTFALSE;

	if (m_Unknown98 > g_AckTimeout)
	{
		if (g_NetShowTimeouts > 0)
			dsi_ServerError("Ack timeout (%.3f)", m_Unknown98);

		return LTTRUE;
	}

	return LTFALSE;
}

void CRateTracker::Update(float fDelta)
{
	m_Time += fDelta;

	if (m_Unknown08 != 0.0f && m_Time > m_Unknown08)
		Rollover();
}

void CRateTracker::Rollover()
{
	float fRate = m_Value / m_Time;
	float fWindow = 0.1f;

	m_Time  = fWindow;
	m_Value = fRate * fWindow;
}

NetConnection::~NetConnection() { *m_Lifetime = false; Term(); }
void NetConnection::Term() {
    if (m_pDriver && m_pDriver->host()) m_pDriver->host()->release_buffers(*this);
    // Drivers own their connections; their host must release bank records
    // before either owner goes away.
    assert(!m_SendQueue.m_nMessages && !m_GuaranteedQueue.m_nMessages &&
           !m_RecvList.m_nMessages && !m_SentList.m_nMessages);
}
