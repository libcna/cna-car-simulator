#include "CarSim/Sim/RigidBody.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim::Sim;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

TEST(RigidBody, FreeFallMatchesAnalyticSolution)
{
    RigidBody body(10.0f, Vector3(1.0f, 1.0f, 1.0f));
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120; ++i) {
        body.ApplyCentralForce(Vector3(0.0f, -98.1f, 0.0f));
        body.Integrate(dt);
    }
    EXPECT_NEAR(body.LinearVelocity().Y, -9.81f, 1e-3f);
    // Semi-implicit Euler lands slightly below the exact -4.905 m: 0.5*g*t*(t+dt).
    EXPECT_NEAR(body.Position().Y, -0.5f * 9.81f * 1.0f * (1.0f + dt), 1e-2f);
}

TEST(RigidBody, TorqueAboutYawAxisRotatesRightToForward)
{
    RigidBody body(1.0f, Vector3(2.0f, 2.0f, 2.0f));
    body.SetAngularVelocity(Vector3(0.0f, 1.0f, 0.0f));
    const float dt = 1.0f / 240.0f;
    const int steps = static_cast<int>(std::round((3.14159265f / 2.0f) / dt));
    for (int i = 0; i < steps; ++i) {
        body.Integrate(dt);
    }
    // A positive rotation about +Y (right-handed) takes +X towards -Z (XNA Forward).
    const Vector3 rotatedRight = body.Right();
    EXPECT_NEAR(rotatedRight.X, 0.0f, 0.02f);
    EXPECT_NEAR(rotatedRight.Z, -1.0f, 0.02f);
    const Vector3 rotatedForward = body.Forward();
    EXPECT_NEAR(rotatedForward.X, -1.0f, 0.02f);
    EXPECT_NEAR(rotatedForward.Z, 0.0f, 0.02f);
}

TEST(RigidBody, ForceAtOffsetProducesTorqueAndVelocityAtPoint)
{
    RigidBody body(2.0f, Vector3(4.0f, 4.0f, 4.0f));
    body.ApplyForce(Vector3(0.0f, 0.0f, -8.0f), Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(body.AccumulatedTorque().Y, 8.0f, 1e-5f);   // r x F = (1,0,0) x (0,0,-8) = (0, 8, 0)
    body.Integrate(0.5f);
    EXPECT_NEAR(body.LinearVelocity().Z, -2.0f, 1e-5f);
    EXPECT_NEAR(body.AngularVelocity().Y, 1.0f, 1e-5f);
    const Vector3 v = body.VelocityAtWorldPoint(body.Position() + Vector3(1.0f, 0.0f, 0.0f));
    // omega x r = (0,1,0) x (1,0,0) = (0,0,-1)
    EXPECT_NEAR(v.Z, -3.0f, 1e-4f);
}

TEST(RigidBody, ImpulseChangesVelocities)
{
    RigidBody body(4.0f, Vector3(8.0f, 8.0f, 8.0f));
    body.ApplyImpulse(Vector3(4.0f, 0.0f, 0.0f), body.Position() + Vector3(0.0f, 0.0f, 2.0f));
    EXPECT_NEAR(body.LinearVelocity().X, 1.0f, 1e-5f);
    // r x J = (0,0,2) x (4,0,0) = (0, 8, 0) -> omega_y = 8 / 8 = 1
    EXPECT_NEAR(body.AngularVelocity().Y, 1.0f, 1e-5f);
}

TEST(RigidBody, BodyPointRoundTrip)
{
    RigidBody body(1.0f, Vector3(1.0f, 1.0f, 1.0f));
    body.SetOrientation(Quaternion::CreateFromAxisAngle(Vector3::Up, 0.7f));
    body.SetPosition(Vector3(3.0f, 1.0f, -2.0f));
    const Vector3 local(0.5f, -0.25f, 1.5f);
    const Vector3 world = body.ToWorldPoint(local);
    const Vector3 back = body.ToBodyPoint(world);
    EXPECT_NEAR(back.X, local.X, 1e-5f);
    EXPECT_NEAR(back.Y, local.Y, 1e-5f);
    EXPECT_NEAR(back.Z, local.Z, 1e-5f);
}
