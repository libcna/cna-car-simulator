#include "CarSim/Sim/Transmission.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Sim;

namespace
{
    constexpr float kDt = 1.0f / 120.0f;

    TransmissionContext Ctx(float rpm, float throttle, float speed, float clutchPedal, bool running = true)
    {
        TransmissionContext c;
        c.dt = kDt;
        c.engineRpm = rpm;
        c.throttle = throttle;
        c.speedMs = speed;
        c.clutchPedal = clutchPedal;
        c.engineRunning = running;
        return c;
    }

    void StepFor(Transmission& t, const TransmissionContext& c, float seconds)
    {
        const int steps = static_cast<int>(seconds / kDt) + 1;
        for (int i = 0; i < steps; ++i) {
            t.Step(c);
        }
    }
}

TEST(ManualTransmission, RatiosAndLabels)
{
    const auto def = MakeReferenceVehicle();
    ManualTransmission t(def.gearbox);
    EXPECT_EQ(t.Gear(), 0);
    EXPECT_FLOAT_EQ(t.Ratio(), 0.0f);
    EXPECT_EQ(t.DisplayLabel(), "N");
    EXPECT_EQ(t.ForwardGearCount(), 5);
}

TEST(ManualTransmission, ShiftNeedsClutchWhileRunning)
{
    const auto def = MakeReferenceVehicle();
    ManualTransmission t(def.gearbox);
    t.RequestShiftUp();
    t.Step(Ctx(900.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(t.GrindEvent());
    EXPECT_EQ(t.Gear(), 0);

    t.RequestShiftUp();
    t.Step(Ctx(900.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(t.GrindEvent());
    EXPECT_TRUE(t.IsShifting());
    EXPECT_EQ(t.Gear(), 0);
    EXPECT_EQ(t.TargetGear(), 1);
    StepFor(t, Ctx(900.0f, 0.0f, 0.0f, 1.0f), def.gearbox.shiftTimeS);
    EXPECT_FALSE(t.IsShifting());
    EXPECT_EQ(t.Gear(), 1);
    EXPECT_FLOAT_EQ(t.Ratio(), def.gearbox.ratios[0]);
    EXPECT_FLOAT_EQ(t.TotalRatio(), def.gearbox.ratios[0] * def.gearbox.finalDrive);
}

TEST(ManualTransmission, EngineOffStationaryAllowsLeverWithoutClutch)
{
    const auto def = MakeReferenceVehicle();
    ManualTransmission t(def.gearbox);
    t.RequestGear(-1);
    StepFor(t, Ctx(0.0f, 0.0f, 0.0f, 0.0f, false), def.gearbox.shiftTimeS + 0.05f);
    EXPECT_EQ(t.Gear(), -1);
    EXPECT_LT(t.Ratio(), 0.0f);
    EXPECT_EQ(t.DisplayLabel(), "R");
}

TEST(ManualTransmission, CannotExceedTopGearOrBelowReverse)
{
    const auto def = MakeReferenceVehicle();
    ManualTransmission t(def.gearbox);
    t.RequestGear(5);
    StepFor(t, Ctx(3000.0f, 0.3f, 20.0f, 1.0f), def.gearbox.shiftTimeS + 0.05f);
    ASSERT_EQ(t.Gear(), 5);
    t.RequestShiftUp();
    StepFor(t, Ctx(3000.0f, 0.3f, 20.0f, 1.0f), def.gearbox.shiftTimeS + 0.05f);
    EXPECT_EQ(t.Gear(), 5);
    t.RequestGear(-3);
    StepFor(t, Ctx(0.0f, 0.0f, 0.0f, 1.0f), def.gearbox.shiftTimeS + 0.05f);
    EXPECT_EQ(t.Gear(), -1);
}

class AutomaticTest : public ::testing::Test
{
protected:
    VehicleDefinition def = MakeReferenceVehicle();
    AutomaticTransmission t{def.gearbox};

    void EngageDrive()
    {
        t.RequestSelector(AutomaticSelector::Drive, 0.0f);
        StepFor(t, Ctx(800.0f, 0.0f, 0.0f, 0.0f), def.gearbox.shiftTimeS + 0.05f);
        // Let the shift-interval timer expire so decisions are not suppressed.
        StepFor(t, Ctx(800.0f, 0.0f, 0.0f, 0.0f), def.gearbox.automatic.minShiftIntervalS + 0.05f);
    }
};

TEST_F(AutomaticTest, StartsInParkAndEngagesDrive)
{
    EXPECT_EQ(t.Selector(), AutomaticSelector::Park);
    EXPECT_EQ(t.DisplayLabel(), "P");
    EngageDrive();
    EXPECT_EQ(t.Gear(), 1);
    EXPECT_EQ(t.DisplayLabel(), "D1");
}

TEST_F(AutomaticTest, ReverseIsLockedOutAtSpeed)
{
    EngageDrive();
    t.RequestSelector(AutomaticSelector::Reverse, 10.0f);
    EXPECT_TRUE(t.GrindEvent());
    EXPECT_EQ(t.Selector(), AutomaticSelector::Drive);
    t.RequestSelector(AutomaticSelector::Reverse, 0.2f);
    StepFor(t, Ctx(800.0f, 0.0f, 0.0f, 0.0f), def.gearbox.shiftTimeS + 0.05f);
    EXPECT_EQ(t.Gear(), -1);
    EXPECT_EQ(t.DisplayLabel(), "R");
}

TEST_F(AutomaticTest, UpshiftsAtLightThrottleAndHoldsAtFullThrottle)
{
    EngageDrive();
    EXPECT_EQ(t.DecideGear(Ctx(3000.0f, 0.3f, 10.0f, 0.0f)), 2) << "light throttle at 3000 rpm should upshift";
    EXPECT_EQ(t.DecideGear(Ctx(2000.0f, 0.3f, 6.0f, 0.0f)), 1) << "below the upshift point stay in gear";
    EXPECT_EQ(t.DecideGear(Ctx(5000.0f, 1.0f, 15.0f, 0.0f)), 1) << "full throttle holds the gear until near redline";
    EXPECT_EQ(t.DecideGear(Ctx(6100.0f, 1.0f, 18.0f, 0.0f)), 2) << "near the limiter it must upshift";
}

TEST_F(AutomaticTest, DownshiftsWhenLuggingAndOnKickdown)
{
    EngageDrive();
    const float cycle = def.gearbox.automatic.minShiftIntervalS + def.gearbox.shiftTimeS + 0.1f;
    StepFor(t, Ctx(3000.0f, 0.3f, 10.0f, 0.0f), 0.05f);   // shift 1 -> 2 begins
    StepFor(t, Ctx(3000.0f, 0.3f, 15.0f, 0.0f), cycle);   // completes, then 2 -> 3
    StepFor(t, Ctx(3000.0f, 0.3f, 20.0f, 0.0f), cycle);   // 3 -> 4
    StepFor(t, Ctx(2500.0f, 0.3f, 25.0f, 0.0f), cycle);   // hold in 4th
    ASSERT_EQ(t.Gear(), 4);
    EXPECT_EQ(t.DecideGear(Ctx(1500.0f, 0.5f, 12.0f, 0.0f)), 3) << "lugging below the downshift point";
    EXPECT_EQ(t.DecideGear(Ctx(3500.0f, 1.0f, 30.0f, 0.0f)), 3) << "kick-down at full throttle";
    EXPECT_EQ(t.DecideGear(Ctx(2700.0f, 0.4f, 30.0f, 0.0f)), 4) << "cruising: no change";
}

TEST_F(AutomaticTest, RespectsMinimumShiftInterval)
{
    EngageDrive();
    StepFor(t, Ctx(3000.0f, 0.3f, 10.0f, 0.0f), def.gearbox.shiftTimeS + 0.05f);
    ASSERT_EQ(t.Gear(), 2);
    // Immediately lugging: the box must wait before shifting again.
    StepFor(t, Ctx(1000.0f, 0.3f, 10.0f, 0.0f), 0.5f);
    EXPECT_EQ(t.Gear(), 2);
    StepFor(t, Ctx(1000.0f, 0.3f, 10.0f, 0.0f), def.gearbox.automatic.minShiftIntervalS + def.gearbox.shiftTimeS);
    EXPECT_EQ(t.Gear(), 1);
}

TEST_F(AutomaticTest, CouplingCreepsAtIdleAndLocksAboveIdle)
{
    const float idle = def.engine.idleRpm;
    const float maxCap = def.clutch.maxTorqueNm;
    EXPECT_FLOAT_EQ(t.CouplingCapacity(idle, idle, maxCap), def.gearbox.automatic.creepTorqueNm);
    EXPECT_FLOAT_EQ(t.CouplingCapacity(idle - 200.0f, idle, maxCap), def.gearbox.automatic.creepTorqueNm);
    EXPECT_FLOAT_EQ(t.CouplingCapacity(idle + def.gearbox.automatic.lockupSlipRpm, idle, maxCap), maxCap);
    const float mid = t.CouplingCapacity(idle + 0.5f * def.gearbox.automatic.lockupSlipRpm, idle, maxCap);
    EXPECT_GT(mid, def.gearbox.automatic.creepTorqueNm);
    EXPECT_LT(mid, maxCap);
}

TEST(TransmissionFactory, MakesRequestedMode)
{
    const auto def = MakeReferenceVehicle();
    EXPECT_EQ(MakeTransmission(def.gearbox, TransmissionMode::Manual)->Mode(), TransmissionMode::Manual);
    EXPECT_EQ(MakeTransmission(def.gearbox, TransmissionMode::Automatic)->Mode(), TransmissionMode::Automatic);
}
