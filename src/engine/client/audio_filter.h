#pragma once
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace nolf {
struct AudioFilterParameter { std::string_view name; float initial,minimum,maximum; };
struct AudioFilterDescription { std::string_view name; std::span<const AudioFilterParameter> parameters; };
// Stateful native DSP. Authored names/units stay in this adapter; the mixer
// only supplies interleaved samples and owns the allocation budget.
class AudioFilter {
    struct State;
    std::unique_ptr<State> state_;
public:
    AudioFilter(std::string_view,unsigned rate);
    ~AudioFilter();
    AudioFilter(const AudioFilter&)=delete;
    AudioFilter& operator=(const AudioFilter&)=delete;
    static std::span<const AudioFilterDescription> catalog();
    bool parameter(std::string_view,float);
    std::optional<float> parameter(std::string_view) const;
    std::unique_ptr<AudioFilter> reconfigure(unsigned rate) const;
    void process(float& left,float& right);
    unsigned interpolation_order() const;
    std::size_t bytes() const;
};
}
