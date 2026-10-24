#pragma once
#include "ltbasetypes.h"

class CBaseCounter {
public:
    explicit CBaseCounter(int mode);
    void Start();
    uint32 Read(); // milliseconds
    uint32 ReadRaw(); // SDK microseconds
    uint32 sub_10005fab() { return Read(); }
    uint32 m_Data[2]{};
private:
    uint64 elapsed() const;
};
void cnt_StartCounter(CBaseCounter*);
uint32 cnt_EndCounter(CBaseCounter*);
void cnt_Init();
struct LTCounter;
void ci_StartCounter(LTCounter*);
uint32 ci_EndCounter(LTCounter*);
extern uint64 g_CounterScale;
extern CBaseCounter g_NetCounter;
class CountAdder {
public:
    explicit CountAdder(uint32* total) : m_Counter(1), m_pTotal(total) {}
    ~CountAdder();
    CBaseCounter m_Counter;
    uint32* m_pTotal;
};
class CRateController {
public:
    CRateController();
    LTBOOL Update();
    float GetRate();
    void SetRate(float);
    CBaseCounter m_Counter;
    float m_Rate = 0, m_Accum = 0;
};
