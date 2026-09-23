#include "CarSim/Render/WindscreenRain.hpp"
#include "CarSim/Sim/Electrics.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim;

TEST(Wipers, CycleThroughModesAndAlwaysPark)
{
    const auto def = Sim::MakeReferenceVehicle();
    Sim::Electrics e(def.electrics);
    EXPECT_EQ(e.Wipers(), Sim::WiperMode::Off);
    e.CycleWipers();
    EXPECT_EQ(e.Wipers(), Sim::WiperMode::Intermittent);
    e.CycleWipers();
    EXPECT_EQ(e.Wipers(), Sim::WiperMode::Slow);
    const float dt = 1.0f / 60.0f;
    float highest = 0.0f;
    for (int i = 0; i < 40; ++i) {
        e.Step(dt, true);
        highest = std::max(highest, e.WiperPosition());
    }
    EXPECT_GT(highest, 0.5f) << "a slow wipe is under way";
    e.CycleWipers();
    e.CycleWipers();   // fast -> off in the middle of a wipe
    EXPECT_EQ(e.Wipers(), Sim::WiperMode::Off);
    for (int i = 0; i < 120; ++i) e.Step(dt, true);
    EXPECT_FLOAT_EQ(e.WiperPosition(), 0.0f) << "the blades finish the wipe and park";
    for (int i = 0; i < 120; ++i) e.Step(dt, true);
    EXPECT_FLOAT_EQ(e.WiperPosition(), 0.0f) << "and stay parked";
}

TEST(Wipers, IntermittentPausesBetweenWipes)
{
    const auto def = Sim::MakeReferenceVehicle();
    Sim::Electrics e(def.electrics);
    e.CycleWipers();
    int moving = 0;
    const int steps = static_cast<int>((Sim::Electrics::kWipeSlowS + Sim::Electrics::kIntermittentPauseS) * 60.0f);
    for (int i = 0; i < steps; ++i) {
        e.Step(1.0f / 60.0f, true);
        moving += e.WiperPosition() > 0.0f ? 1 : 0;
    }
    EXPECT_NEAR(static_cast<float>(moving) / 60.0f, Sim::Electrics::kWipeSlowS, 0.1f);
    Sim::Electrics off(def.electrics);
    off.CycleWipers();
    for (int i = 0; i < 60; ++i) off.Step(1.0f / 60.0f, false);
    EXPECT_FLOAT_EQ(off.WiperPosition(), 0.0f) << "no wiping with the ignition off";
}

TEST(WindscreenRain, DropsGatherInTheRainAndTheWipersClearThem)
{
    Render::WindscreenRain rain;
    rain.SetGlass(1.3f, 0.75f);
    for (int i = 0; i < 300; ++i) rain.Update(1.0f / 60.0f, 1.0f, 0.0f, 0.0f);
    const std::size_t before = rain.Drops().size();
    EXPECT_GT(before, 100u);
    // One full sweep up and back, no new rain.
    for (int i = 0; i <= 60; ++i) {
        const float t = static_cast<float>(i) / 60.0f;
        rain.Update(1.0f / 600.0f, 0.0f, 0.0f, 0.5f - 0.5f * std::cos(t * 6.2831853f));
    }
    EXPECT_LT(rain.Drops().size(), before / 2) << "a sweep clears most of the screen";
    EXPECT_GT(rain.Drops().size(), 0u) << "the corners the blades cannot reach stay wet";
}

TEST(WindscreenRain, AirflowPushesDropsUpAtSpeedAndTheScreenDriesAfterTheRain)
{
    Render::WindscreenRain rain;
    rain.SetGlass(1.3f, 0.75f);
    for (int i = 0; i < 120; ++i) rain.Update(1.0f / 60.0f, 1.0f, 0.0f, 0.0f);
    float before = 0.0f;
    for (const auto& d : rain.Drops()) before += d.position.Y;
    before /= static_cast<float>(rain.Drops().size());
    for (int i = 0; i < 60; ++i) rain.Update(1.0f / 60.0f, 0.0f, 30.0f, 0.0f);
    float after = 0.0f;
    for (const auto& d : rain.Drops()) after += d.position.Y;
    after /= static_cast<float>(std::max<std::size_t>(1, rain.Drops().size()));
    EXPECT_GT(after, before);
    for (int i = 0; i < 60 * 60; ++i) rain.Update(1.0f / 60.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(rain.Drops().empty());
}
