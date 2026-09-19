#pragma once

#include "CarSim/Sim/Vehicle.hpp"

namespace CarSim::App
{
    inline constexpr float kWalkingSpeedKmh = 4.0f;
    inline constexpr float kRunningSpeedKmh = 8.0f;

    [[nodiscard]] inline bool CanEnterWalking(const Sim::VehicleState& car)
    {
        return !car.flightMode && car.engineState == Sim::EngineState::Off && car.speedKmh <= 0.5f;
    }
}
