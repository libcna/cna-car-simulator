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

TEST(ChaseCamera, SitsBehindTheCarAndAimsAheadForEveryHeading)
{
    // Regression: the orbit basis used (-sin, 0, cos) for "behind", which is the mirror image
    // of -forward for any heading off the north-south axis (the camera ended up in front of an
    // east-bound car and beside a north-west-bound one).
    for (const float yawDeg : {0.0f, 90.0f, 180.0f, 270.0f, -88.3f, 32.8f, 135.0f}) {
        const float yaw = yawDeg * 3.14159265f / 180.0f;
        ChaseCamera cam;
        const Vector3 origin(100.0f, 0.0f, -50.0f);
        auto state = StateAt(origin, yaw, 0.0f);
        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const Vector3 right = state.worldMatrix.getRightProperty();
        cam.Snap(state);
        for (int i = 0; i < 240; ++i) {
            cam.Update(state, 1.0f / 60.0f);
        }
        const Vector3 toCamera = cam.Pose().position - origin;
        EXPECT_NEAR(Vector3::Dot(toCamera, forward), -cam.distance, 0.05f) << "heading " << yawDeg;
        EXPECT_NEAR(Vector3::Dot(toCamera, right), 0.0f, 0.05f) << "heading " << yawDeg;
        const Vector3 toTarget = cam.Pose().target - origin;
        EXPECT_GT(Vector3::Dot(toTarget, forward), 0.5f) << "heading " << yawDeg;
        EXPECT_NEAR(Vector3::Dot(toTarget, right), 0.0f, 0.05f) << "heading " << yawDeg;
        // --chase-yaw 90 orbits to the car's right-hand side.
        cam.yawOffset = 3.14159265f * 0.5f;
        cam.Update(state, 10.0f);
        EXPECT_GT(Vector3::Dot(cam.Pose().position - origin, right), cam.distance * 0.9f) << "heading " << yawDeg;
    }
}
