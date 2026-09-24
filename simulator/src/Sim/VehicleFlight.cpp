// Flight-mode entry and deterministic flight dynamics, kept separate from road-car stepping.
#include "CarSim/Sim/Vehicle.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;

    void Vehicle::ToggleFlight(const GroundSurface& ground)
    {
        flightMode_ = !flightMode_;
        if (flightMode_) {
            const Vector3 origin = OriginPosition();
            // Start high enough for road cars to pass under the skids. The pilot can still
            // descend later, at which point real hull/car contact is resolved.
            const float lift = std::max(3.0f, ground.HeightAt(origin.X, origin.Z) + 3.0f - origin.Y);
            body_.SetPosition(body_.Position() + Vector3(0.0f, lift, 0.0f));
            body_.SetLinearVelocity(Vector3(0.0f, 0.0f, 0.0f));
            body_.SetAngularVelocity(Vector3(0.0f, 0.0f, 0.0f));
            body_.ClearAccumulators();
            flightYaw_ = std::atan2(-body_.Forward().X, -body_.Forward().Z);
            for (auto& wheel : wheels_) wheel.grounded = false;
        } else {
            const Vector3 origin = OriginPosition();
            PlaceAt(Vector3(origin.X, ground.HeightAt(origin.X, origin.Z), origin.Z), flightYaw_);
        }
    }

    void Vehicle::StepFlight(const DriverControls& controls, const float dt, const GroundSurface& ground)
    {
        const TurboMode mode = engine_.TurboSetting();
        const float maxSpeed = mode == TurboMode::UltraUltra ? 500.0f / 3.6f :
                               mode == TurboMode::Ultra ? 111.0f : mode == TurboMode::Turbo ? 69.0f : 36.0f;
        const float acceleration = mode == TurboMode::UltraUltra ? 44.0f :
                                   mode == TurboMode::Ultra ? 42.0f : mode == TurboMode::Turbo ? 22.0f : 10.0f;
        const float climbSpeed = mode == TurboMode::UltraUltra ? 40.0f :
                                mode == TurboMode::Ultra ? 35.0f : mode == TurboMode::Turbo ? 20.0f : 10.0f;
        flightYaw_ -= std::clamp(controls.steering, -1.0f, 1.0f) * 1.4f * dt;
        body_.SetOrientation(Quaternion::CreateFromAxisAngle(Vector3::Up, flightYaw_));
        const Vector3 desired = body_.Forward() * ((std::clamp(controls.throttle, 0.0f, 1.0f) -
                                                     std::clamp(controls.brake, 0.0f, 1.0f)) * maxSpeed);
        Vector3 velocity = body_.LinearVelocity();
        Vector3 horizontal(velocity.X, 0.0f, velocity.Z);
        Vector3 change = desired - horizontal;
        const float changeLength = change.Length();
        if (changeLength > acceleration * dt) change = change * (acceleration * dt / changeLength);
        horizontal = horizontal + change;
        const float verticalTarget = (controls.flightClimb ? climbSpeed : 0.0f) -
                                     (controls.flightDescend ? climbSpeed : 0.0f);
        velocity = Vector3(horizontal.X, std::clamp(verticalTarget - velocity.Y, -acceleration * dt, acceleration * dt) + velocity.Y,
                           horizontal.Z);
        Vector3 position = body_.Position() + velocity * dt;
        const Vector3 origin = OriginPosition();
        const float floor = ground.HeightAt(origin.X, origin.Z) + 1.8f;
        const float originHeight = origin.Y + velocity.Y * dt;
        if (originHeight < floor) {
            position.Y += floor - originHeight;
            velocity.Y = std::max(0.0f, velocity.Y);
        }
        body_.SetPosition(position);
        body_.SetLinearVelocity(velocity);
        rotorAngle_ = std::fmod(rotorAngle_ + dt * (mode == TurboMode::UltraUltra ? 53.0f :
                                                  mode == TurboMode::Ultra ? 43.0f : mode == TurboMode::Turbo ? 33.0f : 25.0f),
                                2.0f * std::numbers::pi_v<float>);
        lastSpeedMs_ = Vector3::Dot(velocity, body_.Forward());
    }

}
