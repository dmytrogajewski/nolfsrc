#include "world_state.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
uint8 channel(float value) { return static_cast<uint8>(std::clamp(value, 0.f, 255.f)); }
}
void WorldShadeGrid::Clear() { samples_.clear(); dimensions_ = {}; origin_.Init(); cell_ = 1; }
void WorldShadeGrid::Setup(LTVector min, LTVector max, float cell, LTVector ambient) {
    if (!finite(min) || !finite(max) || !finite(ambient) || !std::isfinite(cell) || cell <= 0) throw std::runtime_error("invalid shade grid");
    std::array<uint32,3> dimensions{};
    uint64 count = 1;
    for (uint32 axis = 0; axis < 3; ++axis) {
        const double extent = (double(max[axis]) - min[axis]) / cell;
        if (extent < 0 || extent > 64000000) throw std::runtime_error("invalid shade grid extent");
        dimensions[axis] = static_cast<uint32>(extent) + 1; count *= dimensions[axis];
        if (count > (256 * 1024 * 1024) / 4) throw std::runtime_error("shade grid allocation budget");
    }
    std::vector<std::array<uint8,4>> samples(count, {channel(ambient.z),channel(ambient.y),channel(ambient.x),255});
    samples_.swap(samples); dimensions_ = dimensions; origin_ = min; cell_ = cell;
}
void WorldShadeGrid::AddLight(const WorldLight& light) {
    if (samples_.empty() || !finite(light.position) || !finite(light.color) || !std::isfinite(light.radius) || light.radius <= 0) return;
    std::array<uint32,3> min{}, max{};
    for (uint32 axis = 0; axis < 3; ++axis) {
        const auto clamp = [&](double point) { return static_cast<uint32>(std::clamp(point, 0., double(dimensions_[axis] - 1))); };
        min[axis] = clamp((double(light.position[axis]) - light.radius - origin_[axis]) / cell_);
        max[axis] = clamp((double(light.position[axis]) + light.radius - origin_[axis]) / cell_);
    }
    for (uint32 z = min[2]; z <= max[2]; ++z) for (uint32 y = min[1]; y <= max[1]; ++y) for (uint32 x = min[0]; x <= max[0]; ++x) {
        const LTVector point = origin_ + LTVector(float(x),float(y),float(z)) * cell_;
        const float distance = point.Dist(light.position);
        if (distance >= light.radius) continue;
        const float fraction = 1 - distance / light.radius;
        auto& sample = samples_[x + uint64(dimensions_[0]) * (y + uint64(dimensions_[1]) * z)];
        sample[0] = channel(sample[0] + light.color.z * fraction * fraction);
        sample[1] = channel(sample[1] + light.color.y * fraction * fraction);
        sample[2] = channel(sample[2] + light.color.x * fraction * fraction);
    }
}
std::array<uint8,4> WorldShadeGrid::Sample(LTVector point) const {
    if (samples_.empty() || !finite(point)) return {0,0,0,255};
    std::array<uint32,3> near{}, far{}; std::array<float,3> fraction{};
    for (uint32 axis = 0; axis < 3; ++axis) {
        const auto position = std::clamp((double(point[axis]) - origin_[axis]) / cell_, 0., double(dimensions_[axis] - 1));
        near[axis] = static_cast<uint32>(position); far[axis] = std::min(near[axis] + 1, dimensions_[axis] - 1);
        fraction[axis] = static_cast<float>(position - near[axis]);
    }
    std::array<float,3> sum{};
    for (uint32 corner = 0; corner < 8; ++corner) {
        const auto x = corner & 1 ? far[0] : near[0], y = corner & 2 ? far[1] : near[1], z = corner & 4 ? far[2] : near[2];
        const float weight = (corner & 1 ? fraction[0] : 1-fraction[0]) * (corner & 2 ? fraction[1] : 1-fraction[1]) * (corner & 4 ? fraction[2] : 1-fraction[2]);
        const auto& sample = samples_[x + uint64(dimensions_[0]) * (y + uint64(dimensions_[1]) * z)];
        for (uint32 channel = 0; channel < 3; ++channel) sum[channel] += sample[channel] * weight;
    }
    return {channel(sum[0]),channel(sum[1]),channel(sum[2]),255};
}
void w_GetPointShade(WorldShadeGrid* grid, LTVector* point, uint8* color) {
    if (!color) return;
    auto value = grid && point ? grid->Sample(*point) : std::array<uint8,4>{0,0,0,255};
    std::copy(value.begin(), value.end(), color);
}
