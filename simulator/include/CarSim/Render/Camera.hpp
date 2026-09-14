// Camera poses and the two driving cameras (cockpit and chase).
#pragma once

#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>

namespace CarSim::Render
{
    struct CameraPose
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Matrix = Microsoft::Xna::Framework::Matrix;

        Vector3 position{0.0f, 2.0f, 8.0f};
        Vector3 target{0.0f, 1.0f, 0.0f};
        Vector3 up{0.0f, 1.0f, 0.0f};
        float fieldOfViewDeg = 62.0f;
        float nearPlane = 0.08f;
        float farPlane = 2600.0f;

        [[nodiscard]] Matrix View() const;
        [[nodiscard]] Matrix Projection(float aspect) const;
        [[nodiscard]] Vector3 Forward() const;
        [[nodiscard]] Microsoft::Xna::Framework::BoundingFrustum Frustum(float aspect) const;
    };

    enum class CameraMode
    {
        Cockpit,
        Chase
    };

    /// Smooth exterior follow camera: critically damped position and yaw follow, distance and
    /// height that grow with speed, a lateral look-ahead in turns, and a ground clearance
    /// clamp so the camera never dips into slopes or kerbs. See docs/cameras.md.
    class ChaseCamera
    {
    public:
        void Snap(const Sim::VehicleState& state);
        void Update(const Sim::VehicleState& state, float dt);
        [[nodiscard]] const CameraPose& Pose() const { return pose_; }

        float distance = 6.2f;
        float height = 2.0f;
        float targetHeight = 0.9f;
        /// Extra rotation of the camera around the vehicle (radians, positive = camera moves to
        /// the vehicle's right). Used for screenshots and inspection; 0 is directly behind.
        float yawOffset = 0.0f;
        /// Minimum height of the eye above the ground under it (metres).
        float groundClearance = 0.7f;
        /// Ground height query (world x, z); unset = no clearance clamp.
        std::function<float(float, float)> groundHeight;

        [[nodiscard]] float SmoothedYaw() const { return smoothedYaw_; }
        [[nodiscard]] float LookAhead() const { return lookAhead_; }

    private:
        CameraPose pose_;
        Microsoft::Xna::Framework::Vector3 smoothedPosition_{};
        float smoothedYaw_ = 0.0f;
        float previousBodyYaw_ = 0.0f;
        float yawRate_ = 0.0f;       // filtered body yaw rate (rad/s)
        float lookAhead_ = 0.0f;     // lateral target offset (metres, positive = right)
        bool initialised_ = false;
    };

    /// Driver's eye camera attached to the vehicle body.
    class CockpitCamera
    {
    public:
        void Update(const Sim::VehicleState& state, const Sim::VehicleDefinition& definition, float dt);
        [[nodiscard]] const CameraPose& Pose() const { return pose_; }

        /// Inspection offsets (vehicle frame metres; yaw positive = look left, pitch positive = up).
        Microsoft::Xna::Framework::Vector3 eyeOffset{};
        float yawOffsetDeg = 0.0f;
        float pitchOffsetDeg = 0.0f;

    private:
        CameraPose pose_;
        float lateralOffset_ = 0.0f;
        float previousLateralVelocity_ = 0.0f;
    };
}
