#include "CarSim/Sim/Engine.hpp"
#include "CarSim/Sim/EngineThermal.hpp"
#include "CarSim/Sim/FuelSystem.hpp"
#include "CarSim/Sim/Odometer.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;

namespace
{
    constexpr float kDt = 1.0f / 120.0f;

    void RunEngine(Engine& engine, float seconds, float throttle, float load = 0.0f)
    {
        const int steps = static_cast<int>(seconds / kDt);
        for (int i = 0; i < steps; ++i) {
            engine.Step(kDt, throttle, true, true, load);
        }
    }
}

class FuelTest : public ::testing::Test
{
protected:
    VehicleDefinition def = MakeReferenceVehicle();
    Engine engine{def.engine};
    FuelSystem fuel{def.fuel, def.engine.fuel};
};

TEST_F(FuelTest, StartsWithInitialLitersAndNoConsumptionWhenOff)
{
    EXPECT_FLOAT_EQ(fuel.Liters(), def.fuel.initialLiters);
    fuel.Step(10.0f, engine);
    EXPECT_FLOAT_EQ(fuel.Liters(), def.fuel.initialLiters);
    EXPECT_FLOAT_EQ(fuel.LitersPerHour(), 0.0f);
}

TEST_F(FuelTest, IdleConsumptionMatchesDefinitionAndFullThrottleIsMuchHigher)
{
    engine.RequestStart();
    RunEngine(engine, 4.0f, 0.0f);
    fuel.Step(kDt, engine);
    EXPECT_NEAR(fuel.LitersPerHour(), def.engine.fuel.idleLitersPerHour, 0.25f);

    // Rev up, then hold a 100 Nm load at wide-open throttle (roughly 45 kW brake power).
    RunEngine(engine, 1.0f, 1.0f);
    RunEngine(engine, 2.0f, 1.0f, 100.0f);
    fuel.Step(kDt, engine);
    EXPECT_GT(fuel.LitersPerHour(), 8.0f);
    EXPECT_LT(fuel.LitersPerHour(), 25.0f);
}

TEST_F(FuelTest, ReserveWarningAndConfigurableAutomaticRefill)
{
    engine.RequestStart();
    RunEngine(engine, 3.0f, 0.0f);
    fuel.SetLiters(def.fuel.reserveLiters + 0.01f);
    EXPECT_FALSE(fuel.ReserveWarning());
    fuel.SetLiters(def.fuel.reserveLiters - 0.01f);
    EXPECT_TRUE(fuel.ReserveWarning());

    const float refillPoint = def.fuel.reserveLiters * def.fuel.refillAtReserveFraction;   // 3.5 L
    fuel.SetLiters(refillPoint + 0.0005f);
    EXPECT_EQ(fuel.RefillCount(), 0);
    // Burn a little: idle consumption over a few seconds crosses the threshold.
    for (int i = 0; i < 120 * 20 && fuel.RefillCount() == 0; ++i) {
        fuel.Step(kDt, engine);
    }
    EXPECT_EQ(fuel.RefillCount(), 1);
    EXPECT_FLOAT_EQ(fuel.Liters(), def.fuel.tankLiters * def.fuel.refillToFraction);
    EXPECT_FALSE(fuel.ReserveWarning());

    // The rule is data-driven: a different fraction moves the refill point.
    VehicleDefinition other = MakeReferenceVehicle();
    other.fuel.refillAtReserveFraction = 0.25f;
    other.fuel.refillToFraction = 0.5f;
    FuelSystem otherFuel(other.fuel, other.engine.fuel);
    otherFuel.SetLiters(other.fuel.reserveLiters * 0.25f + 0.0005f);
    for (int i = 0; i < 120 * 20 && otherFuel.RefillCount() == 0; ++i) {
        otherFuel.Step(kDt, engine);
    }
    EXPECT_EQ(otherFuel.RefillCount(), 1);
    EXPECT_FLOAT_EQ(otherFuel.Liters(), other.fuel.tankLiters * 0.5f);
}

TEST(EngineThermal, WarmsUpUnderLoadSettlesNearOperatingAndCoolsWhenOff)
{
    const VehicleDefinition def = MakeReferenceVehicle();
    Engine engine(def.engine);
    FuelSystem fuel(def.fuel, def.engine.fuel);
    EngineThermal thermal(def.engine.thermal);
    EXPECT_FLOAT_EQ(thermal.CoolantC(), def.engine.thermal.ambientC);

    engine.RequestStart();
    RunEngine(engine, 3.0f, 0.0f);
    // Ten minutes of moderate driving load (30 % throttle against 15 Nm, 15 m/s).
    for (int i = 0; i < 120 * 600; ++i) {
        engine.Step(kDt, 0.3f, true, true, 15.0f);
        thermal.Step(kDt, engine, fuel.MassFlowGramsPerSecond(engine), 15.0f);
    }
    EXPECT_GT(thermal.CoolantC(), 80.0f);
    EXPECT_LT(thermal.CoolantC(), 100.0f);
    EXPECT_FALSE(thermal.Warning());

    engine.RequestStop();
    for (int i = 0; i < 120 * 1800; ++i) {
        engine.Step(kDt, 0.0f, true, true, 0.0f);
        thermal.Step(kDt, engine, 0.0f, 0.0f);
    }
    EXPECT_LT(thermal.CoolantC(), 45.0f);
}

TEST(Odometer, AccumulatesDistanceIndependentOfDirection)
{
    Odometer odo;
    odo.SetTotalKm(12345.6);
    for (int i = 0; i < 120; ++i) {
        odo.Add(10.0f, 1.0f / 120.0f);
    }
    for (int i = 0; i < 120; ++i) {
        odo.Add(-2.0f, 1.0f / 120.0f);
    }
    EXPECT_NEAR(odo.TripKm(), 0.012, 1e-6);
    EXPECT_NEAR(odo.TotalKm(), 12345.612, 1e-6);
    odo.ResetTrip();
    EXPECT_DOUBLE_EQ(odo.TripKm(), 0.0);
}
