#pragma once

#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Collision/Shapes.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <algorithm>
#include <cmath>
#include <span>

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

    [[nodiscard]] inline bool CanReturnToCar(
        const Microsoft::Xna::Framework::Vector3& walker,
        const Microsoft::Xna::Framework::Vector3& car)
    {
        const float dx = walker.X - car.X;
        const float dz = walker.Z - car.Z;
        return dx * dx + dz * dz <= 3.2f * 3.2f && std::fabs(walker.Y - car.Y) <= 1.2f;
    }

    /// Check full traffic bodies, including buses and lorries whose centre can be farther
    /// than a short fixed-distance filter while their nose still reaches the walker.
    [[nodiscard]] inline bool WalkingOverlapsTraffic(
        const Microsoft::Xna::Framework::Vector3& position,
        const std::span<const Traffic::TrafficVehicle> vehicles)
    {
        using Microsoft::Xna::Framework::Vector3;
        const Collision::Obb walker = Collision::Obb::FromHeading(
            position + Vector3(0.0f, 0.9f, 0.0f), Vector3(0.24f, 0.85f, 0.24f), 0.0f);
        Collision::Contact contact;
        for (const auto& car : vehicles) {
            const Collision::Obb body = Collision::Obb::FromHeading(
                car.position + Vector3(0.0f, car.heightM * 0.5f, 0.0f),
                Vector3(car.widthM * 0.5f, car.heightM * 0.5f, car.lengthM * 0.5f), car.headingRad);
            if (Collision::IntersectObbObb(walker, body, contact)) return true;
        }
        return false;
    }
}
