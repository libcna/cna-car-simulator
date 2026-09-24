#pragma once

#include "CarSim/Sim/Vehicle.hpp"

#include <algorithm>

namespace CarSim::App
{
    inline constexpr float kWalkingSpeedKmh = 6.0f;
    inline constexpr float kRunningSpeedKmh = 16.0f;

    /// Horizontal on-foot velocity with a short start/stop ramp. The caller resolves each axis
    /// against the world and zeros an axis that hits a wall, so the walker cannot accelerate
    /// through a blocked surface.
    [[nodiscard]] inline Microsoft::Xna::Framework::Vector3 StepWalkingVelocity(
        const Microsoft::Xna::Framework::Vector3& current,
        const Microsoft::Xna::Framework::Vector3& direction, const bool running, const float dt)
    {
        using Microsoft::Xna::Framework::Vector3;
        Vector3 target(0.0f, 0.0f, 0.0f);
        if (direction.LengthSquared() > 0.001f) {
            target = direction;
            target.Normalize();
            target *= (running ? kRunningSpeedKmh : kWalkingSpeedKmh) / 3.6f;
        }
        const Vector3 difference = target - current;
        const float remaining = difference.Length();
        const float rate = target.LengthSquared() > 0.0f ? 5.0f : 8.0f;
        const float step = rate * std::clamp(dt, 0.0f, 0.1f);
        return remaining <= step || remaining < 1e-5f ? target : current + difference * (step / remaining);
    }

    [[nodiscard]] inline bool CanEnterWalking(const Sim::VehicleState& car)
    {
        return !car.flightMode && car.engineState == Sim::EngineState::Off && car.speedKmh <= 0.5f;
    }
}
