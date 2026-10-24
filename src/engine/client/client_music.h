#pragma once
#include "engine/portable/assets.h"
#include "platform/audio.h"
#include "platform/music.h"
#include "iltclient.h"
#include "iltdirectmusic.h"

namespace nolf {
struct MusicServices {
    std::shared_ptr<const Assets> assets;
    platform::MusicPerformanceFactory performance;
    platform::AudioOutputFactory output;
};
// The engine's adaptive music: level control files define intensities and
// the transitions between them; this schedules their segments exactly as the
// shipped manager's pending queue did. Synthesis is the platform performance,
// output an independent device stream. Secondary segments and motifs, which
// no original level triggers, report LT_ERROR. Pause is a no-op, as shipped.
class ClientMusic final : public ILTDirectMusicMgr {
    struct State;
    std::unique_ptr<State> state_;
public:
    explicit ClientMusic(MusicServices);
    ~ClientMusic();
    ClientMusic(const ClientMusic&)=delete;
    ClientMusic& operator=(const ClientMusic&)=delete;
    // Synthesizes ahead of the device; the host calls this every frame.
    void pump();
    LTRESULT Init() override;
    LTRESULT Term() override;
    LTRESULT InitLevel(const char*,const char*,const char*,const char*,const char*) override;
    LTRESULT TermLevel() override;
    LTRESULT Play() override;
    LTRESULT Stop(const LTDMEnactTypes) override;
    LTRESULT Pause(const LTDMEnactTypes) override;
    LTRESULT UnPause() override;
    LTRESULT SetVolume(const long) override;
    LTRESULT ChangeIntensity(const int,const LTDMEnactTypes) override;
    LTRESULT PlaySecondary(const char*,const LTDMEnactTypes) override;
    LTRESULT StopSecondary(const char*,const LTDMEnactTypes) override;
    LTRESULT PlayMotif(const char*,const char*,const LTDMEnactTypes) override;
    LTRESULT StopMotif(const char*,const char*,const LTDMEnactTypes) override;
    int GetCurIntensity() override;
    LTDMEnactTypes StringToEnactType(const char*) override;
    void EnactTypeToString(LTDMEnactTypes,char*) override;
    int GetNumIntensities() override;
    int GetInitialIntensity() override;
    long GetInitialVolume() override;
};
}
