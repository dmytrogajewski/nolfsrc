#pragma once
#include "engine/client/surface_display.h"
#include "engine/portable/assets.h"
#include "platform/video.h"
#include "iltclient.h"
#include "iltvideomgr.h"

namespace nolf {
struct VideoServices {
    std::shared_ptr<const Assets> assets;
    platform::VideoDecoderFactory video;
    platform::AudioDecoderFactory audio;
    platform::AudioOutputFactory output;
    // Monotonic seconds. Movies without a playable soundtrack follow it.
    std::function<double()> clock;
};
// Movies replace the engine's Bink module. A soundtrack owns an independent
// output and is the master clock; the picture follows it and drops late
// frames. Texture videos report LT_NOTINITIALIZED: no original level uses one.
class ClientVideos final : public ILTVideoMgr {
    struct State;
    std::unique_ptr<State> state_;
public:
    // The display must outlive this service.
    ClientVideos(VideoServices,SurfaceDisplay&,std::uint32_t width,std::uint32_t height);
    ~ClientVideos();
    ClientVideos(const ClientVideos&)=delete;
    ClientVideos& operator=(const ClientVideos&)=delete;
    void resize(std::uint32_t width,std::uint32_t height);
    // Keeps soundtracks queued between the game's UpdateVideo calls.
    void pump();
    std::size_t active() const;
    LTRESULT StartOnScreenVideo(char*,uint32,HVIDEO&) override;
    LTRESULT StartTextureVideo(char*,uint32,HVIDEO&) override;
    LTRESULT UpdateVideo(HVIDEO) override;
    LTRESULT GetVideoStatus(HVIDEO) override;
    LTRESULT StopVideo(HVIDEO) override;
    LTRESULT BindTextureVideoToPoly(HVIDEO,HPOLY) override;
};
}
