#include "CarSim/Sim/Odometer.hpp"

#include <cmath>

namespace CarSim::Sim
{
    void Odometer::Add(const float groundSpeedMs, const float dt)
    {
        const double ds = static_cast<double>(std::fabs(groundSpeedMs)) * static_cast<double>(dt);
        totalMeters_ += ds;
        tripMeters_ += ds;
    }
}
