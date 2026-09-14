#include "CarSim/Sim/Engine.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;

namespace
{
    constexpr float kDt = 1.0f / 120.0f;

    void RunFree(Engine& engine, float seconds, float throttle, bool fuel = true, float load = 0.0f)
    {
        const int steps = static_cast<int>(seconds / kDt);
        for (int i = 0; i < steps; ++i) {
            engine.Step(kDt, throttle, fuel, true, load);
        }
    }
}

class EngineTest : public ::testing::Test
{
protected:
    VehicleDefinition def = MakeReferenceVehicle();
    Engine engine{def.engine};
};

TEST_F(EngineTest, StartsOffAndProducesNothing)
{
    EXPECT_EQ(engine.State(), EngineState::Off);
    EXPECT_FLOAT_EQ(engine.Rpm(), 0.0f);
    EXPECT_FLOAT_EQ(engine.CombustionTorque(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(engine.EffectiveThrottle(1.0f), 0.0f);
}

TEST_F(EngineTest, StarterCranksThenCatchesAndSettlesAtIdle)
{
    engine.RequestStart();
    EXPECT_EQ(engine.State(), EngineState::Starting);

    RunFree(engine, 0.4f, 0.0f);
    EXPECT_EQ(engine.State(), EngineState::Starting) << "must still be cranking before crankSeconds";
    EXPECT_GT(engine.Rpm(), 150.0f) << "starter should spin the crank";
    EXPECT_LT(engine.Rpm(), def.engine.starter.crankRpm * 1.2f) << "starter is speed limited";

    RunFree(engine, 0.6f, 0.0f);
    EXPECT_EQ(engine.State(), EngineState::Running);
    EXPECT_GT(engine.Rpm(), def.engine.starter.catchRpm * 0.9f);

    RunFree(engine, 3.0f, 0.0f);
    EXPECT_EQ(engine.State(), EngineState::Running);
    EXPECT_NEAR(engine.Rpm(), def.engine.idleRpm, 80.0f);
}

TEST_F(EngineTest, WithoutFuelItCranksForever)
{
    engine.RequestStart();
    RunFree(engine, 3.0f, 0.0f, false);
    EXPECT_EQ(engine.State(), EngineState::Starting);
    EXPECT_NEAR(engine.Rpm(), def.engine.starter.crankRpm, 80.0f);
}

TEST_F(EngineTest, RevLimiterCapsFreeRevving)
{
    engine.RequestStart();
    RunFree(engine, 2.0f, 0.0f);
    RunFree(engine, 4.0f, 1.0f);
    EXPECT_EQ(engine.State(), EngineState::Running);
    EXPECT_LE(engine.Rpm(), def.engine.limiterRpm + 50.0f);
    EXPECT_GE(engine.Rpm(), def.engine.limiterRpm - 400.0f);
    EXPECT_TRUE(engine.LimiterActive());
}

TEST_F(EngineTest, StopTurnsOffAndSpinsDown)
{
    engine.RequestStart();
    RunFree(engine, 2.0f, 0.0f);
    engine.RequestStop();
    EXPECT_EQ(engine.State(), EngineState::Off);
    RunFree(engine, 4.0f, 1.0f);
    EXPECT_FLOAT_EQ(engine.Rpm(), 0.0f);
    EXPECT_FALSE(engine.Injecting());
}

TEST_F(EngineTest, HeavyLoadAtIdleStallsTheEngine)
{
    engine.RequestStart();
    RunFree(engine, 3.0f, 0.0f);
    ASSERT_EQ(engine.State(), EngineState::Running);
    RunFree(engine, 2.0f, 0.0f, true, 150.0f);
    EXPECT_EQ(engine.State(), EngineState::Stalled);
    // A stalled engine can be restarted.
    engine.RequestStart();
    EXPECT_EQ(engine.State(), EngineState::Starting);
}

TEST_F(EngineTest, ToggleStartsAndStops)
{
    engine.Toggle();
    EXPECT_EQ(engine.State(), EngineState::Starting);
    engine.Toggle();
    EXPECT_EQ(engine.State(), EngineState::Off);
}

TEST_F(EngineTest, OverrunCutsInjectionAndLoadFractionTracksThrottle)
{
    engine.RequestStart();
    RunFree(engine, 2.0f, 0.0f);
    RunFree(engine, 1.0f, 1.0f);                 // rev up freely
    RunFree(engine, 1.5f, 1.0f, true, 100.0f);   // then hold a 100 Nm load at wide-open throttle
    EXPECT_GT(engine.LoadFraction(), 0.9f);
    EXPECT_GT(engine.BrakePowerKw(), 20.0f);
    engine.Step(kDt, 0.0f, true, true, 0.0f);
    EXPECT_FALSE(engine.Injecting()) << "closed throttle well above idle should cut fuel";
    RunFree(engine, 5.0f, 0.0f);
    EXPECT_TRUE(engine.Injecting()) << "back at idle the injectors work again";
}
