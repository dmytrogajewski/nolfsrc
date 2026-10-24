#include "counter.h"
#include "ltbasedefs.h"
#include "platform/system.h"
#include <cmath>

uint64 g_CounterScale = 1000;
float dsi_GetTime() {
    static const auto epoch = nolf::platform::monotonic_nanoseconds();
    return static_cast<float>((nolf::platform::monotonic_nanoseconds() - epoch) * 0.000000001);
}

CBaseCounter::CBaseCounter(int mode) { if (mode == 1 || mode == 2) Start(); }
void CBaseCounter::Start() {
    const auto now = nolf::platform::monotonic_nanoseconds();
    m_Data[0] = static_cast<uint32>(now);
    m_Data[1] = static_cast<uint32>(now >> 32);
}
uint64 CBaseCounter::elapsed() const {
    const uint64 start = uint64(m_Data[0]) | (uint64(m_Data[1]) << 32);
    return nolf::platform::monotonic_nanoseconds() - start;
}
uint32 CBaseCounter::Read() { return static_cast<uint32>(elapsed() / 1000000); }
uint32 CBaseCounter::ReadRaw() { return static_cast<uint32>(elapsed() / 1000); }
void cnt_StartCounter(CBaseCounter* counter) { counter->Start(); }
uint32 cnt_EndCounter(CBaseCounter* counter) { return counter->ReadRaw(); }
void cnt_Init() { g_CounterScale = 1000; }
void ci_StartCounter(LTCounter* counter) {
    CBaseCounter started(1);
    counter->m_Data[0] = started.m_Data[0]; counter->m_Data[1] = started.m_Data[1];
    counter->m_Data[2] = counter->m_Data[3] = 0;
}
uint32 ci_EndCounter(LTCounter* counter) {
    CBaseCounter started(0);
    started.m_Data[0] = counter->m_Data[0]; started.m_Data[1] = counter->m_Data[1];
    return started.ReadRaw();
}
CountAdder::~CountAdder() { if (m_pTotal) *m_pTotal += m_Counter.ReadRaw(); }

CRateController::CRateController() : m_Counter(0) {}
float CRateController::GetRate() { return m_Rate; }
void CRateController::SetRate(float rate) {
    m_Rate = std::isfinite(rate) && rate > 0 ? rate : 0;
    m_Accum = 0;
    m_Counter.Start();
}
LTBOOL CRateController::Update() {
    if (m_Rate == 0) return LTFALSE;
    const float interval = 1.0f / m_Rate;
    const float elapsed = m_Counter.ReadRaw() * 0.000001f + m_Accum;
    if (elapsed < interval) return LTFALSE;
    m_Accum = std::fmod(elapsed, interval);
    m_Counter.Start();
    return LTTRUE;
}

float time_GetTime() { return dsi_GetTime(); }
