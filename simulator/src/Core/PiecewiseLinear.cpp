#include "CarSim/Core/PiecewiseLinear.hpp"

#include <algorithm>

namespace CarSim::Core
{
    PiecewiseLinear::PiecewiseLinear(std::vector<std::pair<float, float>> points)
        : points_(std::move(points))
    {
        std::stable_sort(points_.begin(), points_.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    void PiecewiseLinear::AddPoint(const float x, const float y)
    {
        const auto it = std::upper_bound(points_.begin(), points_.end(), x,
                                         [](float value, const auto& p) { return value < p.first; });
        points_.insert(it, {x, y});
    }

    float PiecewiseLinear::Evaluate(const float x) const
    {
        if (points_.empty()) {
            return 0.0f;
        }
        if (x <= points_.front().first) {
            return points_.front().second;
        }
        if (x >= points_.back().first) {
            return points_.back().second;
        }
        const auto upper = std::upper_bound(points_.begin(), points_.end(), x,
                                            [](float value, const auto& p) { return value < p.first; });
        const auto lower = upper - 1;
        const float span = upper->first - lower->first;
        if (span <= 0.0f) {
            return upper->second;
        }
        const float t = (x - lower->first) / span;
        return lower->second + (upper->second - lower->second) * t;
    }

    float PiecewiseLinear::MaxY() const
    {
        if (points_.empty()) {
            return 0.0f;
        }
        return std::max_element(points_.begin(), points_.end(),
                                [](const auto& a, const auto& b) { return a.second < b.second; })->second;
    }

    float PiecewiseLinear::ArgMaxY() const
    {
        if (points_.empty()) {
            return 0.0f;
        }
        return std::max_element(points_.begin(), points_.end(),
                                [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    }
}
