#include "CarSim/Sim/Clutch.hpp"

#include <algorithm>

namespace CarSim::Sim
{
    float Clutch::Engagement(const float pedal01) const
    {
        const float pedal = std::clamp(pedal01, 0.0f, 1.0f);
        if (pedal <= def_.engageStart) {
            return 1.0f;
        }
        if (pedal >= def_.engageEnd) {
            return 0.0f;
        }
        const float t = (pedal - def_.engageStart) / (def_.engageEnd - def_.engageStart);
        // Smoothstep from engaged (t = 0) to open (t = 1).
        const float s = t * t * (3.0f - 2.0f * t);
        return 1.0f - s;
    }

    float Clutch::Capacity(const float pedal01) const
    {
        return def_.maxTorqueNm * Engagement(pedal01);
    }

    float Clutch::PedalForCapacity(const float torqueNm) const
    {
        if (torqueNm <= 0.0f) {
            return def_.engageEnd;
        }
        if (torqueNm >= def_.maxTorqueNm) {
            return def_.engageStart;
        }
        // Capacity decreases monotonically with the pedal: bisect inside the band.
        float lo = def_.engageStart;   // capacity max
        float hi = def_.engageEnd;     // capacity 0
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (Capacity(mid) > torqueNm) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return 0.5f * (lo + hi);
    }
}
