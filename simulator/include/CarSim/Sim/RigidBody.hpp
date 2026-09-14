// Six-degree-of-freedom rigid body with a diagonal body-frame inertia tensor.
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CarSim::Sim
{
    class RigidBody
    {
    public:
        using Vector3 = Microsoft::Xna::Framework::Vector3;
        using Quaternion = Microsoft::Xna::Framework::Quaternion;
        using Matrix = Microsoft::Xna::Framework::Matrix;

        RigidBody();
        RigidBody(float mass, const Vector3& bodyInertia);

        void SetMass(float mass);
        void SetBodyInertia(const Vector3& inertia);

        [[nodiscard]] float Mass() const { return mass_; }
        [[nodiscard]] float InverseMass() const { return inverseMass_; }
        [[nodiscard]] const Vector3& BodyInertia() const { return inertia_; }

        [[nodiscard]] const Vector3& Position() const { return position_; }
        [[nodiscard]] const Quaternion& Orientation() const { return orientation_; }
        [[nodiscard]] const Vector3& LinearVelocity() const { return linearVelocity_; }
        [[nodiscard]] const Vector3& AngularVelocity() const { return angularVelocity_; }

        void SetPosition(const Vector3& position) { position_ = position; }
        void SetOrientation(const Quaternion& orientation);
        void SetLinearVelocity(const Vector3& velocity) { linearVelocity_ = velocity; }
        void SetAngularVelocity(const Vector3& velocity) { angularVelocity_ = velocity; }

        /// Rotation matrix (body to world), refreshed when the orientation changes.
        [[nodiscard]] const Matrix& Rotation() const { return rotation_; }
        [[nodiscard]] Vector3 Forward() const;   // body -Z in world
        [[nodiscard]] Vector3 Up() const;        // body +Y in world
        [[nodiscard]] Vector3 Right() const;     // body +X in world

        [[nodiscard]] Vector3 ToWorldPoint(const Vector3& bodyPoint) const;
        [[nodiscard]] Vector3 ToWorldDirection(const Vector3& bodyDirection) const;
        [[nodiscard]] Vector3 ToBodyDirection(const Vector3& worldDirection) const;
        [[nodiscard]] Vector3 ToBodyPoint(const Vector3& worldPoint) const;

        /// Velocity of a world-space point rigidly attached to the body.
        [[nodiscard]] Vector3 VelocityAtWorldPoint(const Vector3& worldPoint) const;

        /// Accumulates a world-space force acting at the centre of mass.
        void ApplyCentralForce(const Vector3& force);
        /// Accumulates a world-space force acting at a world-space point.
        void ApplyForce(const Vector3& force, const Vector3& worldPoint);
        /// Accumulates a world-space torque.
        void ApplyTorque(const Vector3& torque);

        /// Instantaneous world-space impulse at a world-space point.
        void ApplyImpulse(const Vector3& impulse, const Vector3& worldPoint);

        /// World-space inverse inertia tensor as a matrix (rotation part only).
        [[nodiscard]] Matrix InverseInertiaWorld() const;

        /// Semi-implicit Euler step using and clearing the accumulated forces/torques.
        void Integrate(float dt);

        void ClearAccumulators();

        [[nodiscard]] const Vector3& AccumulatedForce() const { return force_; }
        [[nodiscard]] const Vector3& AccumulatedTorque() const { return torque_; }

    private:
        void RefreshRotation();

        float mass_ = 1.0f;
        float inverseMass_ = 1.0f;
        Vector3 inertia_{1.0f, 1.0f, 1.0f};
        Vector3 inverseInertia_{1.0f, 1.0f, 1.0f};
        Vector3 position_{};
        Quaternion orientation_{0.0f, 0.0f, 0.0f, 1.0f};
        Matrix rotation_;
        Vector3 linearVelocity_{};
        Vector3 angularVelocity_{};
        Vector3 force_{};
        Vector3 torque_{};
    };
}
