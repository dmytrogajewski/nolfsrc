// Native animation cursors. Serialized indices remain 16-bit; arithmetic
// uses wide intermediates so long updates cannot wrap time or blend counters.
#include "commonmodel.h"
#include "model_callback.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
AnimInfo* info(Model* model, uint32 index) {
    ModelRuntimeData* data = model;
    return data && index < data->m_AnimArray.GetSize() ? &data->m_AnimArray[index] : nullptr;
}
void locate(LTAnimTracker& tracker, ModelAnim& anim) {
    const auto time = tracker.m_TimeRef.m_Cur.m_Time;
    auto* begin = anim.m_pKeyFrames;
    auto* end = begin + anim.m_nKeyFrames;
    auto* next = std::upper_bound(begin,end,time,[](uint32 t,const AnimKeyFrame& key) { return t < key.m_Time; });
    const auto current = next == end ? anim.m_nKeyFrames - 1 : static_cast<uint32>(next - begin);
    const auto previous = current && begin[current].m_Time > time ? current - 1 : current;
    tracker.m_TimeRef.m_Prev.m_iAnim = tracker.m_TimeRef.m_Cur.m_iAnim;
    tracker.m_TimeRef.m_Prev.m_iFrame = static_cast<uint16>(previous);
    tracker.m_TimeRef.m_Cur.m_iFrame = static_cast<uint16>(current);
    const auto gap = begin[current].m_Time - begin[previous].m_Time;
    tracker.m_TimeRef.m_Percent = gap ? std::clamp(float(time - begin[previous].m_Time) / float(gap),0.f,1.f) : 0;
}
bool dispatch(LTAnimTracker& tracker, ModelAnim& anim, uint32 end_time) {
    const ModelCallbackGuard guard(tracker);
    while (tracker.m_CurKey < anim.m_nKeyFrames && anim.m_pKeyFrames[tracker.m_CurKey].m_Time < end_time) {
        auto& key = anim.m_pKeyFrames[tracker.m_CurKey++];
        if (key.m_Type == 1 && (tracker.m_Flags & 4) && key.m_pFn) key.m_pFn(&tracker,&key);
        else if (!key.m_Type && key.m_pString && key.m_pString[0] && tracker.m_StringKeyCallback)
            tracker.m_StringKeyCallback(&tracker,&key);
        if (!guard.unchanged()) return false;
    }
    return true;
}
}
AnimInfo* LTAnimTracker::GetCurAnimInfo() { return info(GetModel(),m_TimeRef.m_Cur.m_iAnim); }
AnimInfo* LTAnimTracker::GetPrevAnimInfo() { return info(GetModel(),m_TimeRef.m_Prev.m_iAnim); }
ModelAnim* LTAnimTracker::GetCurAnim() { auto* value = GetCurAnimInfo(); return value ? value->m_pAnim : nullptr; }
ModelAnim* LTAnimTracker::GetPrevAnim() { auto* value = GetPrevAnimInfo(); return value ? value->m_pAnim : nullptr; }
AnimKeyFrame* LTAnimTracker::GetCurFrame() {
    auto* anim = GetCurAnim(); return anim && m_TimeRef.m_Cur.m_iFrame < anim->m_nKeyFrames ? &anim->m_pKeyFrames[m_TimeRef.m_Cur.m_iFrame] : nullptr;
}
AnimKeyFrame* LTAnimTracker::GetPrevFrame() {
    auto* anim = GetPrevAnim(); return anim && m_TimeRef.m_Prev.m_iFrame < anim->m_nKeyFrames ? &anim->m_pKeyFrames[m_TimeRef.m_Prev.m_iFrame] : nullptr;
}
LTBOOL LTAnimTracker::IsValid() { return GetCurFrame() && GetPrevFrame(); }
LTBOOL LTAnimTracker::AllowInvalid() { return (m_Flags & 8) != 0; }
ModelAnim* AnimTimeRef::GetAnim1() { auto* value = info(m_pModel,m_Prev.m_iAnim); return value ? value->m_pAnim : nullptr; }
ModelAnim* AnimTimeRef::GetAnim2() { auto* value = info(m_pModel,m_Cur.m_iAnim); return value ? value->m_pAnim : nullptr; }
LTBOOL AnimTimeRef::SetKeyframePosition(uint32 anim, uint32 first, uint32 second, float percent) {
    auto* value = info(m_pModel,anim);
    if (!value || !value->m_pAnim || first >= value->m_pAnim->m_nKeyFrames || second >= value->m_pAnim->m_nKeyFrames ||
        !std::isfinite(percent) || percent < 0 || percent > 1 || anim > 65535 || first > 65535 || second > 65535) return LTFALSE;
    Init(m_pModel,anim,first,anim,second,percent); return LTTRUE;
}
uint32 ModelAnim::GetLength() { return m_nKeyFrames ? m_pKeyFrames[m_nKeyFrames - 1].m_Time : 0; }
void at_ResetAnim(LTAnimTracker* tracker) {
    if (!tracker) return;
    ++tracker->m_MutationGeneration;
    const auto anim = tracker->m_TimeRef.m_Cur.m_iAnim;
    tracker->m_TimeRef.m_Cur.Clear(); tracker->m_TimeRef.m_Prev.Clear();
    tracker->m_TimeRef.m_Cur.m_iAnim = tracker->m_TimeRef.m_Prev.m_iAnim = anim;
    tracker->m_TimeRef.m_Percent = 0;
    tracker->m_CurKey = 0;
    tracker->m_InterpolationMS = 0xffff;
    tracker->m_InterpolationElapsed = 0;
}
void at_Reset(LTAnimTracker* tracker) { at_ResetAnim(tracker); }
LTBOOL at_SetAnim(LTAnimTracker* tracker, uint32 anim, LTBOOL blend) {
    if (!tracker || anim > 65535 || (!tracker->AllowInvalid() && !info(tracker->GetModel(),anim))) return LTFALSE;
    const auto old = tracker->m_TimeRef.m_Cur;
    const bool valid = tracker->IsValid();
    at_ResetAnim(tracker);
    tracker->m_TimeRef.m_Cur.m_iAnim = static_cast<uint16>(anim);
    tracker->m_TimeRef.m_Prev = tracker->m_TimeRef.m_Cur;
    if (blend && valid && tracker->GetCurAnim()) {
        tracker->m_TimeRef.m_Prev = old;
        tracker->m_InterpolationMS = 0;
    }
    return LTTRUE;
}
void at_SetCurAnim(LTAnimTracker* tracker, HMODELANIM anim, LTBOOL blend) { at_SetAnim(tracker,anim,blend); }
LTBOOL at_IsDone(LTAnimTracker* tracker) {
    if (!tracker || !tracker->IsValid() || (tracker->m_Flags & AT_LOOPING)) return LTFALSE;
    const auto length = tracker->GetCurAnim()->GetLength();
    return tracker->m_TimeRef.m_Cur.m_Time >= length && (length == UINT32_MAX || tracker->m_TimeRef.m_Cur.m_Time > length);
}
void at_Advance(LTAnimTracker* tracker, uint32 elapsed, int events) {
    if (!tracker) return;
    if (!tracker->IsValid()) {
        tracker->m_TimeRef.m_Cur.m_Time = static_cast<uint32>(std::min<uint64>(UINT32_MAX,uint64(tracker->m_TimeRef.m_Cur.m_Time) + elapsed));
        return;
    }
    auto* anim = tracker->GetCurAnim();
    if (tracker->m_InterpolationMS != 0xffff) {
        const uint64 total = tracker->m_InterpolationElapsed + elapsed;
        if (total <= anim->m_nInterpolationTime && anim->m_nInterpolationTime) {
            tracker->m_InterpolationElapsed = total;
            tracker->m_InterpolationMS = static_cast<uint16>(std::min<uint64>(65534,total));
            tracker->m_TimeRef.m_Percent = float(total) / float(anim->m_nInterpolationTime);
            return;
        }
        elapsed = static_cast<uint32>(total - anim->m_nInterpolationTime);
        at_ResetAnim(tracker);
    }
    const auto length = anim->GetLength();
    const uint64 next = uint64(tracker->m_TimeRef.m_Cur.m_Time) + elapsed;
    const bool loop = (tracker->m_Flags & AT_LOOPING) && length;
    const auto limit = static_cast<uint32>(std::min<uint64>(UINT32_MAX,uint64(length) + 1));
    tracker->m_TimeRef.m_Prev.m_Time = tracker->m_TimeRef.m_Cur.m_Time;
    tracker->m_TimeRef.m_Cur.m_Time = static_cast<uint32>(loop && next > length ? next % length : std::min<uint64>(next,limit));
    // Like the original engine, a stalled update dispatches at most one
    // complete loop. This bounds work even for a multi-day delta.
    if (events && !dispatch(*tracker,*anim,static_cast<uint32>(std::min<uint64>(next,limit)))) return;
    if (loop && next > length) {
        tracker->m_CurKey = 0;
        if (events && !dispatch(*tracker,*anim,tracker->m_TimeRef.m_Cur.m_Time)) return;
    }
    locate(*tracker,*anim);
    if (!events) tracker->m_CurKey = static_cast<uint32>(std::lower_bound(anim->m_pKeyFrames,anim->m_pKeyFrames + anim->m_nKeyFrames,tracker->m_TimeRef.m_Cur.m_Time,
        [](const AnimKeyFrame& key,uint32 time) { return key.m_Time < time; }) - anim->m_pKeyFrames);
}
void at_SetTime(LTAnimTracker* tracker, uint32 time, LTBOOL interpolate) {
    if (!tracker) return;
    at_ResetAnim(tracker);
    at_Advance(tracker,time,0);
    if (!interpolate) { tracker->m_TimeRef.m_Prev = tracker->m_TimeRef.m_Cur; tracker->m_TimeRef.m_Percent = 0; }
}
void at_UpdateTracker(LTAnimTracker* tracker, uint32 elapsed) { if (tracker && (tracker->m_Flags & AT_PLAYING)) at_Advance(tracker,elapsed,1); }
AnimKeyFrame* at_FindNextStringKey(LTAnimTracker* tracker, uint32 first, uint32* found) {
    auto* anim = tracker ? tracker->GetCurAnim() : nullptr;
    if (anim) for (uint32 i = first; i < anim->m_nKeyFrames; ++i) if (!anim->m_pKeyFrames[i].m_Type) {
        if (found) *found = i;
        return &anim->m_pKeyFrames[i];
    }
    return nullptr;
}
