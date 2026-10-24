#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nolf::platform {
// When a primary segment starts. Segment waits for the playing one to end.
enum class MusicTiming { Instant,Grid,Beat,Measure,Segment };
class MusicSegment {
public:
    virtual ~MusicSegment()=default;
    virtual double length() const=0;
};
// A style's short phrase, layered over whatever segment is playing.
class MusicMotif {
public:
    virtual ~MusicMotif()=default;
};
struct MusicPlayback {
    // Changes whenever a segment starts, repeats or playback stops.
    std::uint32_t serial=0;
    // Frames until the playing segment ends at the current tempo.
    std::size_t remaining=0;
    bool playing=false;
};
// Interactive music synthesis over segments, styles and DLS banks. Rendering
// is pulled by the caller, so scheduling is exact to the rendered frame.
// Methods and destruction belong to the creating thread.
class MusicPerformance {
public:
    virtual ~MusicPerformance()=default;
    // Later loads resolve and cache files independently of earlier ones:
    // music directories reuse file names for different instruments.
    virtual void scope()=0;
    // Null when the resolver cannot supply the segment or a file it needs.
    virtual std::shared_ptr<const MusicSegment> load(std::string_view name)=0;
    // A null segment stops playback at the given boundary.
    virtual void play(const std::shared_ptr<const MusicSegment>&,MusicTiming)=0;
    // Null when the resolver cannot supply the style or it has no such motif.
    virtual std::shared_ptr<const MusicMotif> load_motif(std::string_view style,std::string_view name)=0;
    // Starts at the boundary over the playing segment, which it neither
    // replaces nor delays; stopping playback cuts it off.
    virtual void play_motif(const std::shared_ptr<const MusicMotif>&,MusicTiming)=0;
    virtual void volume(float gain)=0;
    virtual MusicPlayback playback() const=0;
    // Interleaved stereo floats at the creation rate.
    virtual void render(std::span<float>)=0;
};
// Supplies a referenced file's bytes by its bare name, or nothing.
using MusicResolver=std::function<std::optional<std::vector<std::byte>>(std::string_view)>;
std::unique_ptr<MusicPerformance> make_dmusic_performance(MusicResolver,unsigned rate);
using MusicPerformanceFactory=std::function<std::unique_ptr<MusicPerformance>(MusicResolver,unsigned)>;
}
