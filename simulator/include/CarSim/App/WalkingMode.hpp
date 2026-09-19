#pragma once

#include "CarSim/Sim/Vehicle.hpp"

namespace CarSim::App
{
    inline constexpr float kWalkingSpeedKmh = 6.0f;
    inline constexpr float kRunningSpeedKmh = 16.0f;

    [[nodiscard]] inline bool CanEnterWalking(const Sim::VehicleState& car)
    {
        return !car.flightMode && car.engineState == Sim::EngineState::Off && car.speedKmh <= 0.5f;
    }
}
