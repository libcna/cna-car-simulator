// Walking entry, occupancy and movement control. The established algorithms are
// kept as SimulatorGame methods so the driving/walking state handoff is unchanged.
#include "CarSim/App/SimulatorGame.hpp"
#include "CarSim/App/WalkingMode.hpp"

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

namespace CarSim::App
{
    using namespace Microsoft::Xna::Framework;

    bool SimulatorGame::WalkingCanOccupy(const Vector3& position) const
    {
        const Collision::Obb walker = Collision::Obb::FromHeading(
            position + Vector3(0.0f, 0.9f, 0.0f), Vector3(0.24f, 0.85f, 0.24f), 0.0f);
        Collision::Contact contact;
        if (collision_.Overlaps(walker) ||
            Collision::IntersectObbObb(walker, Collision::CollisionWorld::VehicleBox(*vehicle_), contact)) {
            return false;
        }
        if (traffic_ && WalkingOverlapsTraffic(position, traffic_->Vehicles())) return false;
        return true;
    }

    void SimulatorGame::ToggleWalking()
    {
        if (walking_) {
            if (!CanReturnToCar(walkingPosition_, vehicle_->Snapshot().originPosition)) {
                std::cout << "walking: move within 3 m of the car to get in\n";
                return;
            }
            walking_ = false;
            running_ = false;
            walkingMoving_ = false;
            walkingVelocity_ = Vector3(0.0f, 0.0f, 0.0f);
            std::cout << "walking: returned to car\n";
            return;
        }

        const Sim::VehicleState state = vehicle_->Snapshot();
        if (!CanEnterWalking(state)) {
            return;
        }

        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const Vector3 right = state.worldMatrix.getRightProperty();
        const float offset = definition_.chassis.widthM * 0.5f + 0.66f;
        const auto groundHeight = [this](const float x, const float z) {
            return map_ ? map_->Ground().HeightAt(x, z) : 0.0f;
        };
        // Prefer the driver's side; use the passenger side if a wall blocks the door.
        for (const float side : {-1.0f, 1.0f}) {
            const Vector3 candidate = state.originPosition + right * (side * offset);
            const Vector3 foot(candidate.X, groundHeight(candidate.X, candidate.Z), candidate.Z);
            if (!WalkingCanOccupy(foot)) continue;
            walking_ = true;
            running_ = false;
            walkingMoving_ = false;
            walkingVelocity_ = Vector3(0.0f, 0.0f, 0.0f);
            walkingPosition_ = foot;
            walkingYaw_ = std::atan2(-forward.X, -forward.Z);
            walkingStepDistance_ = 0.5f;
            walkingBobPhase_ = 0.0f;
            std::cout << "walking: entered on foot\n";
            return;
        }
        std::cout << "walking: no space next to car\n";
    }

    void SimulatorGame::UpdateWalking(const float dt)
    {
        using Microsoft::Xna::Framework::Input::Keyboard;
        using Microsoft::Xna::Framework::Input::Keys;
        const auto keys = Keyboard::GetState();
        const float turn = (keys.IsKeyDown(Keys::Right) ? 1.0f : 0.0f) -
                           (keys.IsKeyDown(Keys::Left) ? 1.0f : 0.0f);
        walkingYaw_ += turn * 2.1f * dt;
        const float forward = (keys.IsKeyDown(Keys::Up) ? 1.0f : 0.0f) -
                              (keys.IsKeyDown(Keys::Down) ? 1.0f : 0.0f);
        const float sideways = (keys.IsKeyDown(Keys::D) ? 1.0f : 0.0f) -
                               (keys.IsKeyDown(Keys::A) ? 1.0f : 0.0f);
        Vector3 direction(std::sin(walkingYaw_) * forward + std::cos(walkingYaw_) * sideways,
                          0.0f,
                          -std::cos(walkingYaw_) * forward + std::sin(walkingYaw_) * sideways);
        walkingMoving_ = false;
        walkingVelocity_ = StepWalkingVelocity(walkingVelocity_, direction, running_, dt);
        const float distance = walkingVelocity_.Length() * std::clamp(dt, 0.0f, 0.25f);
        if (distance < 1e-5f) return;
        const int steps = std::max(1, static_cast<int>(std::ceil(distance / 0.08f)));
        const float stepSeconds = std::clamp(dt, 0.0f, 0.25f) / static_cast<float>(steps);
        const auto groundHeight = [this](const float x, const float z) {
            return map_ ? map_->Ground().HeightAt(x, z) : 0.0f;
        };
        float travelled = 0.0f;
        for (int i = 0; i < steps; ++i) {
            const Vector3 delta = walkingVelocity_ * stepSeconds;
            const Vector3 previous = walkingPosition_;
            Vector3 next(walkingPosition_.X + delta.X, 0.0f, walkingPosition_.Z);
            next.Y = groundHeight(next.X, next.Z);
            if (WalkingCanOccupy(next)) walkingPosition_ = next;
            else walkingVelocity_.X = 0.0f;
            next = Vector3(walkingPosition_.X, 0.0f, walkingPosition_.Z + delta.Z);
            next.Y = groundHeight(next.X, next.Z);
            if (WalkingCanOccupy(next)) walkingPosition_ = next;
            else walkingVelocity_.Z = 0.0f;
            const Vector3 actual = walkingPosition_ - previous;
            travelled += std::hypot(actual.X, actual.Z);
        }
        walkingMoving_ = travelled > 0.001f;
        walkingBobPhase_ += travelled * (2.0f * std::numbers::pi_v<float> / 1.5f);
        walkingStepDistance_ += travelled;
        const float stride = running_ ? 0.95f : 0.72f;
        while (walkingStepDistance_ >= stride) {
            walkingStepDistance_ -= stride;
            if (audio_) audio_->TriggerFootstep();
        }
    }
}
