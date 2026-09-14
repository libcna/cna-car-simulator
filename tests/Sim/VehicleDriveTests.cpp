// Whole-vehicle acceptance drives on simple ground surfaces.
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Units.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

using namespace CarSim::Sim;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    constexpr float kFrame = 1.0f / 60.0f;

    /// Runs `seconds` of simulated time, asking `controlsAt(time)` for the driver intent.
    void Drive(Vehicle& vehicle, const GroundSurface& ground, float seconds,
               const std::function<DriverControls(float)>& controlsAt)
    {
        const int frames = static_cast<int>(std::round(seconds / kFrame));
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) * kFrame;
            vehicle.Update(controlsAt(t), kFrame, ground);
        }
    }

    DriverControls Idle()
    {
        return DriverControls{};
    }

    /// Starts the engine with the clutch pressed and waits for idle.
    void StartEngine(Vehicle& vehicle, const GroundSurface& ground)
    {
        DriverControls c;
        c.clutch = 1.0f;
        c.brake = 1.0f;
        vehicle.Update(c, kFrame, ground);
        c.toggleEngine = true;
        vehicle.Update(c, kFrame, ground);
        c.toggleEngine = false;
        Drive(vehicle, ground, 3.0f, [c](float) { return c; });
        ASSERT_EQ(vehicle.GetEngine().State(), EngineState::Running);
    }

    float Yaw(const Vehicle& v)
    {
        const Vector3 f = v.Body().Forward();
        return std::atan2(-f.X, -f.Z);   // 0 when facing -Z, positive when turned left
    }
}

class VehicleDrive : public ::testing::Test
{
protected:
    VehicleDefinition def = MakeReferenceVehicle();
    FlatGround ground{0.0f};
};

TEST_F(VehicleDrive, SettlesAtRestOnFlatGround)
{
    Vehicle v(def, TransmissionMode::Manual);
    v.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    Drive(v, ground, 4.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    const auto s = v.Snapshot();
    EXPECT_NEAR(s.originPosition.Y, 0.0f, 0.03f) << "the vehicle origin should sit on the ground";
    EXPECT_NEAR(s.originPosition.X, 0.0f, 0.02f);
    EXPECT_NEAR(s.originPosition.Z, 0.0f, 0.02f);
    EXPECT_LT(s.velocity.Length(), 0.02f);
    for (const auto& w : s.wheels) {
        EXPECT_TRUE(w.grounded);
        EXPECT_GT(w.load, 1500.0f);
        EXPECT_LT(w.load, 4500.0f);
    }
    EXPECT_NEAR(v.Body().Up().Y, 1.0f, 1e-3f) << "no roll or pitch on flat ground";
}

TEST_F(VehicleDrive, ManualLaunchWithClutchAndThrottleMovesOffWithoutStalling)
{
    Vehicle v(def, TransmissionMode::Manual);
    StartEngine(v, ground);
    DriverControls c;
    c.clutch = 1.0f;
    c.selectGear = 1;
    v.Update(c, kFrame, ground);
    Drive(v, ground, 0.5f, [](float) { DriverControls k; k.clutch = 1.0f; return k; });
    ASSERT_EQ(v.GetTransmission().Gear(), 1);

    // Throttle to about 2000 rpm, then release the clutch (the pedal model releases it
    // progressively) and keep accelerating.
    Drive(v, ground, 6.0f, [](float) {
        DriverControls k;
        k.throttle = 0.45f;
        k.clutch = 0.0f;
        return k;
    });
    const auto s = v.Snapshot();
    EXPECT_EQ(s.engineState, EngineState::Running) << "a careful launch must not stall";
    EXPECT_GT(s.speedKmh, 20.0f);
    EXPECT_LT(s.speedKmh, 70.0f);
    EXPECT_TRUE(s.clutchLocked);
    EXPECT_GT(s.engineRpm, 1500.0f);
}

TEST_F(VehicleDrive, DumpingTheClutchAtIdleStallsTheEngine)
{
    Vehicle v(def, TransmissionMode::Manual);
    StartEngine(v, ground);
    DriverControls c;
    c.clutch = 1.0f;
    c.selectGear = 1;
    v.Update(c, kFrame, ground);
    Drive(v, ground, 0.5f, [](float) { DriverControls k; k.clutch = 1.0f; return k; });
    ASSERT_EQ(v.GetTransmission().Gear(), 1);
    v.ForcePedals(0.0f, 0.0f, 0.0f);   // clutch dumped instantly, no throttle
    Drive(v, ground, 1.5f, [](float) { return DriverControls{}; });
    EXPECT_EQ(v.GetEngine().State(), EngineState::Stalled);
}

TEST_F(VehicleDrive, AutomaticFullThrottleAccelerationIsPlausible)
{
    Vehicle v(def, TransmissionMode::Automatic);
    DriverControls c;
    c.brake = 1.0f;
    v.Update(c, kFrame, ground);
    c.toggleEngine = true;
    v.Update(c, kFrame, ground);
    c.toggleEngine = false;
    Drive(v, ground, 3.0f, [c](float) { return c; });
    ASSERT_EQ(v.GetEngine().State(), EngineState::Running);
    c.selector = AutomaticSelector::Drive;
    v.Update(c, kFrame, ground);
    c.selector.reset();
    Drive(v, ground, 1.0f, [c](float) { return c; });

    float timeTo100 = -1.0f;
    float t = 0.0f;
    int maxGear = 0;
    while (t < 30.0f && timeTo100 < 0.0f) {
        DriverControls k;
        k.throttle = 1.0f;
        v.Update(k, kFrame, ground);
        t += kFrame;
        maxGear = std::max(maxGear, v.GetTransmission().Gear());
        if (v.SpeedKmh() >= 100.0f) {
            timeTo100 = t;
        }
        ASSERT_EQ(v.GetEngine().State(), EngineState::Running) << "an automatic must never stall";
    }
    EXPECT_GT(timeTo100, 8.0f) << "a 60 kW hatchback is not a sports car";
    EXPECT_LT(timeTo100, 18.0f) << "and it should still reach 100 km/h in a reasonable time";
    EXPECT_GE(maxGear, 3) << "the automatic should have shifted up while accelerating";
    const auto s = v.Snapshot();
    EXPECT_NEAR(s.originPosition.X, 0.0f, 1.0f) << "full-throttle launch should track straight";
}

TEST_F(VehicleDrive, BrakingFrom100KmhStopsWithinRealisticDistance)
{
    Vehicle v(def, TransmissionMode::Automatic);
    Drive(v, ground, 2.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    v.ForceForwardSpeed(Units::KmhToMs(100.0f));
    const Vector3 start = v.OriginPosition();
    float t = 0.0f;
    while (v.SpeedKmh() > 1.0f && t < 15.0f) {
        DriverControls c;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        t += kFrame;
    }
    const float distance = (v.OriginPosition() - start).Length();
    EXPECT_GT(distance, 36.0f);
    EXPECT_LT(distance, 60.0f);
    EXPECT_LT(t, 6.0f);
    EXPECT_NEAR(v.OriginPosition().X, start.X, 1.0f) << "braking must not pull the car sideways";
}

TEST_F(VehicleDrive, TracksStraightAtHighwaySpeed)
{
    Vehicle v(def, TransmissionMode::Automatic);
    Drive(v, ground, 2.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    v.ForceForwardSpeed(Units::KmhToMs(90.0f));
    const float yaw0 = Yaw(v);
    Drive(v, ground, 5.0f, [](float) { return DriverControls{}; });
    EXPECT_NEAR(Yaw(v), yaw0, Units::DegToRad(1.0f));
    EXPECT_NEAR(v.OriginPosition().X, 0.0f, 0.5f);
    EXPECT_GT(v.SpeedKmh(), 70.0f) << "coasting in neutral loses speed only through drag and rolling resistance";
}

TEST_F(VehicleDrive, SteeringRightTurnsRightAndTheSteeringWheelFollows)
{
    Vehicle v(def, TransmissionMode::Automatic);
    Drive(v, ground, 2.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    v.ForceForwardSpeed(Units::KmhToMs(40.0f));
    Drive(v, ground, 3.0f, [](float) { DriverControls c; c.steering = 0.4f; return c; });
    EXPECT_LT(Yaw(v), -Units::DegToRad(15.0f)) << "positive steering input is a right turn (negative yaw)";
    EXPECT_GT(v.OriginPosition().X, 2.0f) << "the car should have moved to the right of its start line";
    EXPECT_GT(v.SteeringWheelAngle(), Units::DegToRad(60.0f));
    EXPECT_LT(v.Body().Up().Y, 1.0f) << "the body should lean in the turn";
    EXPECT_GT(v.Body().Up().Y, 0.99f) << "but not excessively";
}

TEST_F(VehicleDrive, HandbrakeHoldsOnASlope)
{
    // 12 % grade rising towards -z (the car faces uphill).
    const FunctionGround slope([](float, float z) { return -0.12f * z; });
    Vehicle v(def, TransmissionMode::Manual);
    v.PlaceAt(Vector3(0.0f, 0.0f, 0.0f), 0.0f);
    Drive(v, slope, 1.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    const Vector3 start = v.OriginPosition();
    Drive(v, slope, 5.0f, [](float) { DriverControls c; c.handbrake = true; return c; });
    EXPECT_LT((v.OriginPosition() - start).Length(), 0.3f);

    // Without any brake the car rolls back down.
    Drive(v, slope, 4.0f, [](float) { return DriverControls{}; });
    EXPECT_GT((v.OriginPosition() - start).Length(), 2.0f);
    EXPECT_GT(v.OriginPosition().Z, start.Z) << "rolling downhill means moving towards +z";
}

TEST_F(VehicleDrive, DeterministicAcrossRuns)
{
    auto run = [&]() {
        Vehicle v(def, TransmissionMode::Automatic);
        DriverControls c;
        c.toggleEngine = true;
        v.Update(c, kFrame, ground);
        Drive(v, ground, 2.0f, [](float) { return DriverControls{}; });
        DriverControls d;
        d.selector = AutomaticSelector::Drive;
        v.Update(d, kFrame, ground);
        Drive(v, ground, 6.0f, [](float t) {
            DriverControls k;
            k.throttle = 0.7f;
            k.steering = 0.2f * std::sin(t);
            return k;
        });
        return v.Snapshot();
    };
    const auto a = run();
    const auto b = run();
    EXPECT_EQ(a.originPosition.X, b.originPosition.X);
    EXPECT_EQ(a.originPosition.Z, b.originPosition.Z);
    EXPECT_EQ(a.engineRpm, b.engineRpm);
    EXPECT_EQ(a.odometerKm, b.odometerKm);
}

TEST_F(VehicleDrive, OdometerAndFuelRespondToDriving)
{
    Vehicle v(def, TransmissionMode::Automatic);
    DriverControls c;
    c.toggleEngine = true;
    v.Update(c, kFrame, ground);
    Drive(v, ground, 2.0f, [](float) { return DriverControls{}; });
    DriverControls d;
    d.selector = AutomaticSelector::Drive;
    v.Update(d, kFrame, ground);
    const float fuelBefore = v.Fuel().Liters();
    Drive(v, ground, 30.0f, [](float) { DriverControls k; k.throttle = 0.5f; return k; });
    EXPECT_GT(v.GetOdometer().TripKm(), 0.2);
    EXPECT_LT(v.Fuel().Liters(), fuelBefore);
    const float lPer100 = v.Fuel().TripAverageLPer100Km(static_cast<float>(v.GetOdometer().TripKm()));
    EXPECT_GT(lPer100, 3.0f);
    EXPECT_LT(lPer100, 25.0f) << "accelerating from rest is thirsty but not absurd";
    EXPECT_GT(v.Thermal().CoolantC(), def.engine.thermal.ambientC + 1.0f);
}
