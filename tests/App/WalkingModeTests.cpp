#include "CarSim/App/WalkingMode.hpp"
#include "CarSim/Audio/SoundSynth.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

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
