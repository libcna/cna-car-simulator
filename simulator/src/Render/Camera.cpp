#include "CarSim/Render/Camera.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;

    Matrix CameraPose::View() const
    {
        return Matrix::CreateLookAt(position, target, up);
    }

    Matrix CameraPose::Projection(const float aspect) const
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(fieldOfViewDeg), aspect, nearPlane, farPlane);
    }

    Vector3 CameraPose::Forward() const
    {
        Vector3 f = target - position;
        if (f.LengthSquared() > 1e-8f) {
            f.Normalize();
        }
        return f;
    }

    BoundingFrustum CameraPose::Frustum(const float aspect) const
    {
        return BoundingFrustum(View() * Projection(aspect));
    }

    namespace
    {
        float YawOf(const Vector3& forward)
        {
            return std::atan2(-forward.X, -forward.Z);   // 0 facing -Z, positive turning left
        }

        float LerpAngle(const float from, const float to, const float t)
        {
            float delta = to - from;
            while (delta > MathHelper::Pi) delta -= MathHelper::TwoPi;
            while (delta < -MathHelper::Pi) delta += MathHelper::TwoPi;
            return from + delta * t;
        }
    }

    void ChaseCamera::Snap(const Sim::VehicleState& state)
    {
        const Vector3 forward = state.worldMatrix.getForwardProperty();
        smoothedYaw_ = YawOf(forward);
        const Vector3 behind(std::sin(smoothedYaw_), 0.0f, std::cos(smoothedYaw_));
        smoothedPosition_ = state.originPosition + behind * distance + Vector3(0.0f, height, 0.0f);
        initialised_ = true;
        Update(state, 0.0f);
    }

    void ChaseCamera::Update(const Sim::VehicleState& state, const float dt)
    {
        if (!initialised_) {
            Snap(state);
            return;
        }
        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const float bodyYaw = YawOf(forward);
        const float yawRate = 1.0f - std::exp(-dt * 4.0f);
        smoothedYaw_ = LerpAngle(smoothedYaw_, bodyYaw, yawRate);

        const float speedPull = std::clamp(state.speedKmh / 130.0f, 0.0f, 1.0f) * 1.2f;
        // With yaw = 0 the car faces -Z, so "behind" is +Z; rotate that by the smoothed yaw.
        const Vector3 behind(-std::sin(smoothedYaw_), 0.0f, std::cos(smoothedYaw_));
        const float orbitYaw = smoothedYaw_ + yawOffset;
        const Vector3 orbit(-std::sin(orbitYaw), 0.0f, std::cos(orbitYaw));
        const Vector3 desired = state.originPosition + orbit * (distance + speedPull) + Vector3(0.0f, height, 0.0f);
        const float posRate = 1.0f - std::exp(-dt * 9.0f);
        smoothedPosition_ = Vector3::Lerp(smoothedPosition_, desired, posRate);

        pose_.position = smoothedPosition_;
        pose_.target = state.originPosition + Vector3(0.0f, targetHeight, 0.0f) - behind * 1.0f;
        pose_.up = Vector3(0.0f, 1.0f, 0.0f);
        pose_.fieldOfViewDeg = 60.0f;
        pose_.nearPlane = 0.3f;   // depth precision: roads sit 12 cm above the terrain
    }

    void CockpitCamera::Update(const Sim::VehicleState& state, const Sim::VehicleDefinition& definition, const float dt)
    {
        const Vector3 eyeLocal = definition.visual.driverEye;
        const Vector3 right = state.worldMatrix.getRightProperty();
        const float lateralVelocity = Vector3::Dot(state.velocity, right);
        float lateralAccel = 0.0f;
        if (dt > 1e-5f) {
            lateralAccel = (lateralVelocity - previousLateralVelocity_) / dt;
        }
        previousLateralVelocity_ = lateralVelocity;
        const float targetOffset = std::clamp(-lateralAccel * 0.004f, -0.03f, 0.03f);
        lateralOffset_ += (targetOffset - lateralOffset_) * std::min(1.0f, dt * 6.0f);

        const Vector3 eyeBase = eyeLocal + eyeOffset;
        const Vector3 eyeWorld = Vector3::Transform(eyeBase + Vector3(lateralOffset_, 0.0f, 0.0f), state.worldMatrix);
        const float yaw = MathHelper::ToRadians(yawOffsetDeg);
        const float pitch = MathHelper::ToRadians(pitchOffsetDeg) - 0.002f;
        const Vector3 lookDir(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
        const Vector3 lookLocal = eyeBase + lookDir * 10.0f;
        const Vector3 lookWorld = Vector3::Transform(lookLocal, state.worldMatrix);
        pose_.position = eyeWorld;
        pose_.target = lookWorld;
        pose_.up = state.worldMatrix.getUpProperty();
        pose_.fieldOfViewDeg = definition.visual.cockpitFovDeg;
        pose_.nearPlane = 0.12f;   // nothing in the cabin is closer than the A-pillar (~0.3 m)
    }
}
