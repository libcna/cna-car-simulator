// Piecewise-linear 1D curve used for torque curves, shift maps and gauge scales.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace CarSim::Core
{
    /// A monotonic-in-x list of (x, y) points evaluated by linear interpolation
    /// and clamped outside the defined range.
    class PiecewiseLinear
    {
    public:
        PiecewiseLinear() = default;
        explicit PiecewiseLinear(std::vector<std::pair<float, float>> points);

        /// Adds a point; points are kept sorted by x.
        void AddPoint(float x, float y);

        [[nodiscard]] bool Empty() const { return points_.empty(); }
        [[nodiscard]] std::size_t Size() const { return points_.size(); }
        [[nodiscard]] const std::vector<std::pair<float, float>>& Points() const { return points_; }

        /// Linear interpolation, clamped to the first/last y outside the range.
        /// An empty curve evaluates to 0.
        [[nodiscard]] float Evaluate(float x) const;

        /// Largest y on the curve (0 when empty).
        [[nodiscard]] float MaxY() const;

        /// x of the largest y (0 when empty).
        [[nodiscard]] float ArgMaxY() const;

        [[nodiscard]] float MinX() const { return points_.empty() ? 0.0f : points_.front().first; }
        [[nodiscard]] float MaxX() const { return points_.empty() ? 0.0f : points_.back().first; }

    private:
        std::vector<std::pair<float, float>> points_;
    };
}
