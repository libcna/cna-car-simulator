#include "CarSim/App/WalkingMode.hpp"
#include "CarSim/Audio/SoundSynth.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace CarSim;

TEST(WalkingMode, RequiresStoppedCarWithEngineOffAndRejectsHelicopter)
{
    Sim::VehicleState car;
    EXPECT_TRUE(App::CanEnterWalking(car));
    car.speedKmh = 1.0f;
    EXPECT_FALSE(App::CanEnterWalking(car));
    car.speedKmh = 0.0f;
    car.engineState = Sim::EngineState::Running;
    EXPECT_FALSE(App::CanEnterWalking(car));
    car.engineState = Sim::EngineState::Starting;
    EXPECT_FALSE(App::CanEnterWalking(car));
    car.engineState = Sim::EngineState::Stalled;
    EXPECT_FALSE(App::CanEnterWalking(car));
    car.engineState = Sim::EngineState::Off;
    car.flightMode = true;
    EXPECT_FALSE(App::CanEnterWalking(car));
    EXPECT_FLOAT_EQ(App::kWalkingSpeedKmh, 6.0f);
    EXPECT_FLOAT_EQ(App::kRunningSpeedKmh, 16.0f);
}

TEST(WalkingMode, FootstepHasAudibleShortImpact)
{
    const Audio::Clip step = Audio::Clips::Footstep(44100);
    ASSERT_GT(step.samples.size(), 4000u);
    ASSERT_LT(step.samples.size(), 10000u);
    const float peak = *std::max_element(step.samples.begin(), step.samples.end(),
                                         [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    EXPECT_GT(std::fabs(peak), 0.3f);
}

TEST(WalkingMode, StartsAndStopsSmoothlyWithoutExceedingWalkingOrRunningSpeed)
{
    using Microsoft::Xna::Framework::Vector3;
    Vector3 velocity(0.0f, 0.0f, 0.0f);
    const Vector3 forward(0.0f, 0.0f, -1.0f);
    velocity = App::StepWalkingVelocity(velocity, forward, false, 0.1f);
    EXPECT_NEAR(velocity.Length(), 0.5f, 1e-4f);
    for (int i = 0; i < 10; ++i) velocity = App::StepWalkingVelocity(velocity, forward, false, 0.1f);
    EXPECT_NEAR(velocity.Length(), App::kWalkingSpeedKmh / 3.6f, 1e-4f);
    for (int i = 0; i < 10; ++i) velocity = App::StepWalkingVelocity(velocity, forward, true, 0.1f);
    EXPECT_NEAR(velocity.Length(), App::kRunningSpeedKmh / 3.6f, 1e-4f);
    const float previous = velocity.Length();
    velocity = App::StepWalkingVelocity(velocity, Vector3(0.0f, 0.0f, 0.0f), false, 0.1f);
    EXPECT_LT(velocity.Length(), previous);
    for (int i = 0; i < 10; ++i) velocity = App::StepWalkingVelocity(velocity, Vector3(0.0f, 0.0f, 0.0f), false, 0.1f);
    EXPECT_LT(velocity.Length(), 1e-4f);
}

TEST(WalkingMode, LongTrafficBodyBlocksWalkerBeyondFiveMetresFromCentre)
{
    using Microsoft::Xna::Framework::Vector3;
    Traffic::TrafficVehicle bus;
    bus.position = Vector3(0.0f, 0.0f, 6.0f);
    bus.lengthM = 12.0f;
    bus.widthM = 2.5f;
    bus.heightM = 3.2f;
    const Vector3 walker(0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(App::WalkingOverlapsTraffic(walker, std::span<const Traffic::TrafficVehicle>(&bus, 1)));
    bus.position.X = 2.0f;
    EXPECT_FALSE(App::WalkingOverlapsTraffic(walker, std::span<const Traffic::TrafficVehicle>(&bus, 1)));
    bus.position = Vector3(0.0f, -4.0f, 6.0f);
    EXPECT_FALSE(App::WalkingOverlapsTraffic(walker, std::span<const Traffic::TrafficVehicle>(&bus, 1)));
}

TEST(WalkingMode, ReturningToCarRequiresNearbyGroundLevelPosition)
{
    using Microsoft::Xna::Framework::Vector3;
    const Vector3 car(10.0f, 2.0f, 20.0f);
    EXPECT_TRUE(App::CanReturnToCar(Vector3(11.7f, 2.2f, 20.0f), car));
    EXPECT_TRUE(App::CanReturnToCar(Vector3(10.0f, 2.0f, 23.0f), car));
    EXPECT_FALSE(App::CanReturnToCar(Vector3(10.0f, 2.0f, 23.3f), car));
    EXPECT_FALSE(App::CanReturnToCar(Vector3(10.0f, 3.3f, 20.0f), car));
}

TEST(WalkingMode, WalksOverKerbsButCannotSnapAcrossWallsOrLedges)
{
    EXPECT_TRUE(App::CanWalkGroundStep(2.0f, 2.16f));
    EXPECT_TRUE(App::CanWalkGroundStep(2.16f, 2.0f));
    EXPECT_TRUE(App::CanWalkGroundStep(2.0f, 2.20f));
    EXPECT_FALSE(App::CanWalkGroundStep(2.0f, 2.50f));
    EXPECT_FALSE(App::CanWalkGroundStep(2.0f, 1.50f));
    EXPECT_FALSE(App::CanWalkGroundStep(2.0f, std::numeric_limits<float>::quiet_NaN()));
}
