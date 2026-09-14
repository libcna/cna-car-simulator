#include "CarSim/Sim/RigidBody.hpp"

#include <cmath>

namespace CarSim::Sim
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector3;

    RigidBody::RigidBody()
    {
        RefreshRotation();
    }

    RigidBody::RigidBody(const float mass, const Vector3& bodyInertia)
    {
        SetMass(mass);
        SetBodyInertia(bodyInertia);
        RefreshRotation();
    }

    void RigidBody::SetMass(const float mass)
    {
        mass_ = mass > 0.0f ? mass : 1.0f;
        inverseMass_ = 1.0f / mass_;
    }

    void RigidBody::SetBodyInertia(const Vector3& inertia)
    {
        inertia_ = inertia;
        inverseInertia_ = Vector3(inertia.X > 0.0f ? 1.0f / inertia.X : 0.0f,
                                  inertia.Y > 0.0f ? 1.0f / inertia.Y : 0.0f,
                                  inertia.Z > 0.0f ? 1.0f / inertia.Z : 0.0f);
    }

    void RigidBody::SetOrientation(const Quaternion& orientation)
    {
        orientation_ = orientation;
        orientation_.Normalize();
        RefreshRotation();
    }

    void RigidBody::RefreshRotation()
    {
        rotation_ = Matrix::CreateFromQuaternion(orientation_);
    }

    Vector3 RigidBody::Forward() const { return rotation_.getForwardProperty(); }
    Vector3 RigidBody::Up() const { return rotation_.getUpProperty(); }
    Vector3 RigidBody::Right() const { return rotation_.getRightProperty(); }

    Vector3 RigidBody::ToWorldPoint(const Vector3& bodyPoint) const
    {
        return position_ + Vector3::TransformNormal(bodyPoint, rotation_);
    }

    Vector3 RigidBody::ToWorldDirection(const Vector3& bodyDirection) const
    {
        return Vector3::TransformNormal(bodyDirection, rotation_);
    }

    Vector3 RigidBody::ToBodyDirection(const Vector3& worldDirection) const
    {
        // The rotation matrix is orthonormal, so its transpose is its inverse.
        return Vector3(Vector3::Dot(worldDirection, Right()),
                       Vector3::Dot(worldDirection, Up()),
                       -Vector3::Dot(worldDirection, Forward()));
    }

    Vector3 RigidBody::ToBodyPoint(const Vector3& worldPoint) const
    {
        return ToBodyDirection(worldPoint - position_);
    }

    Vector3 RigidBody::VelocityAtWorldPoint(const Vector3& worldPoint) const
    {
        return linearVelocity_ + Vector3::Cross(angularVelocity_, worldPoint - position_);
    }

    void RigidBody::ApplyCentralForce(const Vector3& force)
    {
        force_ = force_ + force;
    }

    void RigidBody::ApplyForce(const Vector3& force, const Vector3& worldPoint)
    {
        force_ = force_ + force;
        torque_ = torque_ + Vector3::Cross(worldPoint - position_, force);
    }

    void RigidBody::ApplyTorque(const Vector3& torque)
    {
        torque_ = torque_ + torque;
    }

    Matrix RigidBody::InverseInertiaWorld() const
    {
        // R * diag(invI) * R^T, expressed through the body axes.
        const Vector3 r = Right();
        const Vector3 u = Up();
        const Vector3 b = -Forward();   // body +Z in world
        Matrix m = Matrix::getIdentityProperty();
        m.M11 = inverseInertia_.X * r.X * r.X + inverseInertia_.Y * u.X * u.X + inverseInertia_.Z * b.X * b.X;
        m.M12 = inverseInertia_.X * r.X * r.Y + inverseInertia_.Y * u.X * u.Y + inverseInertia_.Z * b.X * b.Y;
        m.M13 = inverseInertia_.X * r.X * r.Z + inverseInertia_.Y * u.X * u.Z + inverseInertia_.Z * b.X * b.Z;
        m.M21 = m.M12;
        m.M22 = inverseInertia_.X * r.Y * r.Y + inverseInertia_.Y * u.Y * u.Y + inverseInertia_.Z * b.Y * b.Y;
        m.M23 = inverseInertia_.X * r.Y * r.Z + inverseInertia_.Y * u.Y * u.Z + inverseInertia_.Z * b.Y * b.Z;
        m.M31 = m.M13;
        m.M32 = m.M23;
        m.M33 = inverseInertia_.X * r.Z * r.Z + inverseInertia_.Y * u.Z * u.Z + inverseInertia_.Z * b.Z * b.Z;
        return m;
    }

    void RigidBody::ApplyImpulse(const Vector3& impulse, const Vector3& worldPoint)
    {
        linearVelocity_ = linearVelocity_ + impulse * inverseMass_;
        const Vector3 angularImpulse = Vector3::Cross(worldPoint - position_, impulse);
        angularVelocity_ = angularVelocity_ + Vector3::TransformNormal(angularImpulse, InverseInertiaWorld());
    }

    void RigidBody::Integrate(const float dt)
    {
        linearVelocity_ = linearVelocity_ + force_ * (inverseMass_ * dt);
        angularVelocity_ = angularVelocity_ + Vector3::TransformNormal(torque_, InverseInertiaWorld()) * dt;

        position_ = position_ + linearVelocity_ * dt;

        // q' = q + 0.5 * dt * (omega_world (x) q), Hamilton product with omega as a pure quaternion.
        const Quaternion& q = orientation_;
        const Vector3& w = angularVelocity_;
        const float half = 0.5f * dt;
        Quaternion dq;
        dq.W = half * (-w.X * q.X - w.Y * q.Y - w.Z * q.Z);
        dq.X = half * (w.X * q.W + w.Y * q.Z - w.Z * q.Y);
        dq.Y = half * (w.Y * q.W + w.Z * q.X - w.X * q.Z);
        dq.Z = half * (w.Z * q.W + w.X * q.Y - w.Y * q.X);
        orientation_ = Quaternion(q.X + dq.X, q.Y + dq.Y, q.Z + dq.Z, q.W + dq.W);
        orientation_.Normalize();
        RefreshRotation();

        ClearAccumulators();
    }

    void RigidBody::ClearAccumulators()
    {
        force_ = Vector3(0.0f, 0.0f, 0.0f);
        torque_ = Vector3(0.0f, 0.0f, 0.0f);
    }
}
