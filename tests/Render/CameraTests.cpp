// Chase camera behaviour: frame-rate independent follow, look-ahead in turns, ground clearance.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"
#include "CarSim/Traffic/RouteDriver.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

// Camera stability on a real drive. "It feels jittery" is not something to argue about in prose,
// so this measures it: the car is driven along the sample map's town route by the autopilot, both
// cameras are updated every step, and the frame-to-frame *change in the change* of the camera's
// position -- its jerk -- is measured. A smooth camera glides; a jittery one snaps back and forth
// and shows up as a spike here. The thresholds are set from what this build measures, so a change
// that makes either camera twitchier fails rather than being noticed three passes later.
TEST(ChaseCamera, BothCamerasStaySmoothAlongAWholeRoute)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world) << (errors.empty() ? "" : errors.front());
    ASSERT_FALSE(world->Data().traffic.routes.empty());
    const auto& route = world->Data().traffic.routes.front();

    Sim::VehicleDefinition definition = Sim::MakeReferenceVehicle();
    const auto loaded = Sim::LoadVehicleDefinitionFile(std::string(CARSIM_TEST_CONTENT_DIR) + "/vehicles/lipan_12.json");
    if (loaded.ok()) definition = loaded.definition;
    Sim::Vehicle vehicle(definition, Sim::TransmissionMode::Automatic);
    const Map::SpawnSpec spawn = world->PlayerSpawn(route.spawn);
    vehicle.PlaceAt(world->SpawnPosition(spawn), -spawn.headingDeg * (3.14159265f / 180.0f));

    Traffic::RouteDriver driver(world->Lanes());
    const auto start = vehicle.Snapshot();
    const Vector3 forward = start.worldMatrix.getForwardProperty();
    ASSERT_TRUE(driver.Plan(start.originPosition, std::atan2(forward.X, -forward.Z), route.waypoints));

    ChaseCamera chase;
    chase.groundHeight = [&](const float x, const float z) { return world->Ground().HeightAt(x, z); };
    chase.Snap(start);
    CockpitCamera cockpit;

    const float dt = 1.0f / 60.0f;
    Vector3 previousChase(0.0f, 0.0f, 0.0f);
    Vector3 previousChaseStep(0.0f, 0.0f, 0.0f);
    Vector3 previousCockpit(0.0f, 0.0f, 0.0f);
    Vector3 previousCockpitStep(0.0f, 0.0f, 0.0f);
    double chaseJerkSum = 0.0;
    double cockpitJerkSum = 0.0;
    float worstChaseJerk = 0.0f;
    float worstCockpitJerk = 0.0f;
    int samples = 0;
    float topSpeed = 0.0f;

    for (float t = 0.0f; t < 180.0f; t += dt) {
        const auto state = vehicle.Snapshot();
        vehicle.Update(driver.Update(state, dt), dt, world->Ground());
        const auto after = vehicle.Snapshot();
        chase.Update(after, dt);
        cockpit.Update(after, definition, dt);
        topSpeed = std::max(topSpeed, after.speedKmh);

        // The camera relative to the car: what the *driver* sees moving, not the drive itself.
        const Vector3 chaseLocal = chase.Pose().position - after.originPosition;
        const Vector3 cockpitLocal = cockpit.Pose().position - after.originPosition;
        if (t > 1.0f) {   // skip the settle from Snap
            const Vector3 chaseStep = chaseLocal - previousChase;
            const Vector3 cockpitStep = cockpitLocal - previousCockpit;
            const float chaseJerk = (chaseStep - previousChaseStep).Length();
            const float cockpitJerk = (cockpitStep - previousCockpitStep).Length();
            chaseJerkSum += static_cast<double>(chaseJerk);
            cockpitJerkSum += static_cast<double>(cockpitJerk);
            worstChaseJerk = std::max(worstChaseJerk, chaseJerk);
            worstCockpitJerk = std::max(worstCockpitJerk, cockpitJerk);
            ++samples;
            previousChaseStep = chaseStep;
            previousCockpitStep = cockpitStep;
        }
        previousChase = chaseLocal;
        previousCockpit = cockpitLocal;
        if (driver.Progress().finished) break;
    }

    ASSERT_GT(samples, 3000) << "the route was too short to say anything about the cameras";
    EXPECT_GT(topSpeed, 25.0f) << "the car never got going, so nothing was exercised";
    const float chaseMean = static_cast<float>(chaseJerkSum / samples);
    const float cockpitMean = static_cast<float>(cockpitJerkSum / samples);
    std::cout << "  chase camera: mean jerk " << chaseMean * 1000.0f << " mm/frame^2, worst "
              << worstChaseJerk * 1000.0f << " mm\n"
              << "  cockpit camera: mean jerk " << cockpitMean * 1000.0f << " mm/frame^2, worst "
              << worstCockpitJerk * 1000.0f << " mm\n";
    // This build measures 0.044 mm of change-of-change per frame on both cameras, with the worst
    // single frame at 1.7 mm (chase) and 0.6 mm (cockpit). The bounds leave about four times that
    // headroom: noise will not trip them, a camera that starts twitching will.
    EXPECT_LT(chaseMean, 0.0002f) << "the chase camera shimmers";
    EXPECT_LT(cockpitMean, 0.0002f) << "the cockpit camera shimmers";
    // And no single frame may jump: that is the snap you notice.
    EXPECT_LT(worstChaseJerk, 0.010f) << "the chase camera snapped";
    EXPECT_LT(worstCockpitJerk, 0.010f) << "the cockpit camera snapped";
}
