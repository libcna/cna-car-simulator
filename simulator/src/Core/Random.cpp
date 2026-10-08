#include "CarSim/Core/Random.hpp"

#include <cstdint>

namespace CarSim::Core::Random
{
    int Int(std::mt19937& rng, const int low, const int high)
    {
        if (high <= low) {
            return low;
        }
        const std::uint64_t range = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low) + 1u;
        // The largest multiple of `range` that fits in a draw; draws at or above it are redrawn, so
        // every value in the range is equally likely.
        const std::uint64_t limit = (std::uint64_t{1} << 32) / range * range;
        std::uint64_t draw = rng();
        while (draw >= limit) {
            draw = rng();
        }
        return static_cast<int>(static_cast<std::int64_t>(low) + static_cast<std::int64_t>(draw % range));
    }

    float Float(std::mt19937& rng, const float low, const float high)
    {
        const float unit = static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
        return low + (high - low) * unit;
    }
}
