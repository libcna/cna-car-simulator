#include "CarSim/Traffic/SignalController.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Traffic
{
    const char* ToString(const SignalAspect aspect)
    {
        switch (aspect) {
            case SignalAspect::Red: return "red";
            case SignalAspect::RedAmber: return "red-amber";
            case SignalAspect::Green: return "green";
            case SignalAspect::Amber: return "amber";
        }
        return "?";
    }

    float CycleSeconds(const Map::SignalPlan& plan)
    {
        if (!plan.enabled || plan.groups.size() < 2) {
            return 0.0f;
        }
        const float phase = std::max(1.0f, plan.greenSeconds) + std::max(0.0f, plan.amberSeconds) + std::max(0.0f, plan.allRedSeconds);
        return phase * static_cast<float>(plan.groups.size());
    }

    SignalAspect AspectAt(const Map::SignalPlan& plan, const int group, const float seconds)
    {
        const float cycle = CycleSeconds(plan);
        if (cycle <= 0.0f || group < 0 || static_cast<std::size_t>(group) >= plan.groups.size()) {
            return SignalAspect::Green;   // not signalised: the other rules decide
        }
        const float green = std::max(1.0f, plan.greenSeconds);
        const float amber = std::max(0.0f, plan.amberSeconds);
        const float allRed = std::max(0.0f, plan.allRedSeconds);
        const float phase = green + amber + allRed;
        const int groups = static_cast<int>(plan.groups.size());

        float t = std::fmod(seconds + plan.offsetSeconds, cycle);
        if (t < 0.0f) t += cycle;
        // Where this group's own green starts, and how far we are into the cycle from there.
        const float myStart = phase * static_cast<float>(group);
        float since = t - myStart;
        if (since < 0.0f) since += cycle;

        if (since < green) return SignalAspect::Green;
        if (since < green + amber) return SignalAspect::Amber;
        // Red until the group's turn comes round again; the last second before that is red-amber,
        // which is what a Czech signal shows as "get ready".
        const float untilMyGreen = cycle - since;
        constexpr float kRedAmberSeconds = 1.0f;
        if (allRed > 0.0f && untilMyGreen <= std::min(kRedAmberSeconds, allRed) && groups > 1) {
            return SignalAspect::RedAmber;
        }
        return SignalAspect::Red;
    }
}
