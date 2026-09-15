// Fixed-time traffic signals. Every signalised intersection runs its node's plan: the approach
// groups take their green in turn, each followed by amber and an all-red interval. The aspect
// is a pure function of the elapsed time, so a scenario replayed with the same warm-up sees the
// same lights.
#pragma once

#include "CarSim/Map/RoadNetwork.hpp"

namespace CarSim::Traffic
{
    enum class SignalAspect
    {
        Red,
        RedAmber,   // S 2b: red and amber together, "green is coming"
        Green,
        Amber
    };

    [[nodiscard]] const char* ToString(SignalAspect aspect);

    /// Aspect shown to `group` at an intersection running `plan`, `seconds` after the start.
    /// A plan that is disabled or has fewer than two groups always shows green.
    [[nodiscard]] SignalAspect AspectAt(const Map::SignalPlan& plan, int group, float seconds);

    /// Seconds one full cycle of the plan takes (0 when it is disabled).
    [[nodiscard]] float CycleSeconds(const Map::SignalPlan& plan);

    /// Clock shared by every signalised intersection on the map.
    class SignalController
    {
    public:
        void Update(const float dt) { seconds_ += dt; }
        void Reset() { seconds_ = 0.0f; }
        [[nodiscard]] float Seconds() const { return seconds_; }

        [[nodiscard]] SignalAspect Aspect(const Map::SignalPlan& plan, const int group) const
        {
            return AspectAt(plan, group, seconds_);
        }

    private:
        float seconds_ = 0.0f;
    };
}
