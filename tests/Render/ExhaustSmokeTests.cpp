#include "CarSim/Render/ExhaustSmoke.hpp"

#include <gtest/gtest.h>

using namespace CarSim;
using Microsoft::Xna::Framework::Matrix;

TEST(ExhaustSmoke, OnlyAVisibleRunningCarEmitsFromTheTailpipe)
{
    const Sim::CarStyle style;
    Render::ExhaustSmoke smoke(style);
    Sim::VehicleState car;
    car.worldMatrix = Matrix::getIdentityProperty();

    for (int i = 0; i < 30; ++i) smoke.Update(1.0f / 60.0f, car);
    EXPECT_TRUE(smoke.Puffs().empty());

    car.engineState = Sim::EngineState::Running;
    for (int i = 0; i < 30; ++i) smoke.Update(1.0f / 60.0f, car);
    ASSERT_FALSE(smoke.Puffs().empty());
    EXPECT_LE(smoke.Puffs().size(), static_cast<std::size_t>(Render::ExhaustSmoke::kMaxPuffs));
    EXPECT_NEAR(smoke.Puffs().back().position.X, 0.36f, 0.1f);
    EXPECT_GT(smoke.Puffs().back().position.Z, style.RearZ());

    smoke.SetEnabled(false);
    EXPECT_TRUE(smoke.Puffs().empty());
    for (int i = 0; i < 30; ++i) smoke.Update(1.0f / 60.0f, car);
    EXPECT_TRUE(smoke.Puffs().empty());

    smoke.SetEnabled(true);
    car.flightMode = true;
    for (int i = 0; i < 30; ++i) smoke.Update(1.0f / 60.0f, car);
    EXPECT_TRUE(smoke.Puffs().empty());

    car.flightMode = false;
    for (int i = 0; i < 30; ++i) smoke.Update(1.0f / 60.0f, car);
    EXPECT_FALSE(smoke.Puffs().empty());
    car.engineState = Sim::EngineState::Off;
    for (int i = 0; i < 180; ++i) smoke.Update(1.0f / 60.0f, car);
    EXPECT_TRUE(smoke.Puffs().empty());
}
