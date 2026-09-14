// Chase camera behaviour: frame-rate independent follow, look-ahead in turns, ground clearance.
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim;
using namespace CarSim::Render;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    Sim::VehicleState StateAt(const Vector3& position, const float yawRad, const float speedKmh)
    {
        Sim::VehicleState s;
        s.originPosition = position;
        s.worldMatrix = Matrix::CreateRotationY(yawRad) * Matrix::CreateTranslation(position);
        s.speedKmh = speedKmh;
        s.speedMs = speedKmh / 3.6f;
        return s;
    }
}

TEST(ChaseCamera, SettlesBehindTheCarAndAboveTheGround)
{
    ChaseCamera cam;
    cam.groundHeight = [](float, float) { return 3.0f; };   // a plateau the car drives on
    const auto s = StateAt(Vector3(10.0f, 3.5f, 20.0f), 0.0f, 0.0f);
    cam.Snap(s);
    for (int i = 0; i < 120; ++i) cam.Update(s, 1.0f / 60.0f);
    const CameraPose& p = cam.Pose();
    EXPECT_NEAR(p.position.X, 10.0f, 0.05f);
    EXPECT_NEAR(p.position.Z, 20.0f + cam.distance, 0.1f);   // facing -Z, camera behind at +Z
    EXPECT_GT(p.position.Y, 3.0f + cam.groundClearance - 1e-3f);
    EXPECT_LT(p.position.Y, 3.5f + cam.height + 0.05f);
}

TEST(ChaseCamera, GroundClearanceKeepsTheEyeOutOfSlopes)
{
    ChaseCamera cam;
    cam.groundHeight = [](float, float z) { return z > 25.0f ? 8.0f : 0.0f; };   // a wall of terrain behind the car
    const auto s = StateAt(Vector3(0.0f, 0.4f, 20.0f), 0.0f, 0.0f);
    cam.Snap(s);
    for (int i = 0; i < 60; ++i) cam.Update(s, 1.0f / 60.0f);
    EXPECT_GE(cam.Pose().position.Y, 8.0f + cam.groundClearance - 1e-3f);
}

TEST(ChaseCamera, LooksIntoTheBendAndFollowsIndependentOfFrameRate)
{
    // Turning left at 60 km/h: the aim point slides left (positive look-ahead is right).
    ChaseCamera a, b;
    float yaw = 0.0f;
    const float yawRate = 0.6f;   // rad/s, turning left
    Vector3 pos(0.0f, 0.5f, 0.0f);
    for (int i = 0; i < 180; ++i) {
        yaw += yawRate / 60.0f;
        pos = pos + Vector3(-std::sin(yaw), 0.0f, -std::cos(yaw)) * (60.0f / 3.6f / 60.0f);
        a.Update(StateAt(pos, yaw, 60.0f), 1.0f / 60.0f);
        if (i % 2 == 1) b.Update(StateAt(pos, yaw, 60.0f), 2.0f / 60.0f);
    }
    EXPECT_LT(a.LookAhead(), -0.4f);
    // The 30 Hz camera ends close to the 60 Hz one.
    EXPECT_LT(Vector3::Distance(a.Pose().position, b.Pose().position), 0.8f);
    EXPECT_NEAR(a.SmoothedYaw(), b.SmoothedYaw(), 0.08f);
}
