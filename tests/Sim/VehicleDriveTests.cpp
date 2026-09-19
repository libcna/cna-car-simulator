// Whole-vehicle acceptance drives on simple ground surfaces.
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Units.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

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

TEST_F(VehicleDrive, TurboAcceleratesFasterAndReachesAbout250Kmh)
{
    const auto launch = [this](Vehicle& v, const bool turbo) {
        DriverControls c;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        c.toggleEngine = true;
        c.toggleTurbo = turbo;
        v.Update(c, kFrame, ground);
        c.toggleEngine = false;
        c.toggleTurbo = false;
        Drive(v, ground, 3.0f, [c](float) { return c; });
        EXPECT_EQ(v.GetEngine().State(), EngineState::Running);
        c.selector = AutomaticSelector::Drive;
        v.Update(c, kFrame, ground);
        c.selector.reset();
        Drive(v, ground, 1.0f, [c](float) { return c; });
    };

    Vehicle normal(def, TransmissionMode::Automatic);
    Vehicle boosted(def, TransmissionMode::Automatic);
    launch(normal, false);
    launch(boosted, true);
    EXPECT_EQ(boosted.Snapshot().turboMode, TurboMode::Turbo);
    Drive(normal, ground, 15.0f, [](float) { DriverControls c; c.throttle = 1.0f; return c; });
    Drive(boosted, ground, 15.0f, [](float) { DriverControls c; c.throttle = 1.0f; return c; });
    EXPECT_GT(boosted.SpeedKmh(), normal.SpeedKmh() + 15.0f);

    Drive(boosted, ground, 75.0f, [](float) { DriverControls c; c.throttle = 1.0f; return c; });
    EXPECT_GE(boosted.SpeedKmh(), 245.0f);
    EXPECT_LT(boosted.SpeedKmh(), 255.0f);

    DriverControls off;
    off.toggleTurbo = true;
    boosted.Update(off, kFrame, ground);
    EXPECT_EQ(boosted.Snapshot().turboMode, TurboMode::Ultra);
    boosted.Update(off, kFrame, ground);
    EXPECT_EQ(boosted.Snapshot().turboMode, TurboMode::Off);
}

TEST_F(VehicleDrive, UltraTurboAcceleratesBeyondTurboAndReachesAbout400Kmh)
{
    Vehicle turbo(def, TransmissionMode::Automatic);
    Vehicle ultra(def, TransmissionMode::Automatic);
    const auto launch = [this](Vehicle& v) {
        DriverControls c;
        c.brake = 1.0f;
        v.Update(c, kFrame, ground);
        c.toggleEngine = true;
        c.toggleTurbo = true;
        v.Update(c, kFrame, ground);
        c.toggleEngine = false;
        c.toggleTurbo = false;
        Drive(v, ground, 3.0f, [c](float) { return c; });
        EXPECT_EQ(v.GetEngine().State(), EngineState::Running);
        c.selector = AutomaticSelector::Drive;
        v.Update(c, kFrame, ground);
        c.selector.reset();
        Drive(v, ground, 1.0f, [c](float) { return c; });
    };
    launch(turbo);
    launch(ultra);
    DriverControls next;
    next.toggleTurbo = true;
    ultra.Update(next, kFrame, ground);
    ASSERT_EQ(ultra.Snapshot().turboMode, TurboMode::Ultra);

    const auto fullThrottle = [](float) { DriverControls c; c.throttle = 1.0f; return c; };
    Drive(turbo, ground, 15.0f, fullThrottle);
    Drive(ultra, ground, 15.0f, fullThrottle);
    EXPECT_GT(ultra.SpeedKmh(), turbo.SpeedKmh() + 20.0f);
    Drive(ultra, ground, 120.0f, fullThrottle);
    EXPECT_GE(ultra.SpeedKmh(), 390.0f) << "rpm=" << ultra.GetEngine().Rpm()
                                           << " gear=" << ultra.GetTransmission().Gear()
                                           << " mode=" << ToString(ultra.GetEngine().TurboSetting())
                                           << " engine=" << ToString(ultra.GetEngine().State())
                                           << " fuel=" << ultra.Snapshot().fuelLiters
                                           << " temp=" << ultra.Snapshot().coolantC;
    EXPECT_LT(ultra.SpeedKmh(), 415.0f);
}

TEST_F(VehicleDrive, HelicopterClimbsMovesAndCyclesTurboModes)
{
    Vehicle v(def, TransmissionMode::Automatic);
    DriverControls controls;
    controls.toggleFlight = true;
    v.Update(controls, kFrame, ground);
    ASSERT_TRUE(v.Snapshot().flightMode);
    EXPECT_TRUE(v.Snapshot().lowBeam);
    EXPECT_FALSE(v.Snapshot().highBeam);
    controls.toggleFlight = false;
    controls.toggleHighBeam = true;
    v.Update(controls, kFrame, ground);
    EXPECT_TRUE(v.Snapshot().highBeam);
    controls.toggleHighBeam = false;
    const float takeoffHeight = v.OriginPosition().Y;
    EXPECT_GT(takeoffHeight, 2.0f);

    controls.flightClimb = true;
    Drive(v, ground, 2.0f, [controls](float) { return controls; });
    EXPECT_GT(v.OriginPosition().Y, takeoffHeight + 8.0f);

    controls.flightClimb = false;
    controls.throttle = 1.0f;
    Drive(v, ground, 5.0f, [controls](float) { return controls; });
    EXPECT_GT(v.SpeedKmh(), 100.0f);
    controls.toggleTurbo = true;
    v.Update(controls, kFrame, ground);
    EXPECT_EQ(v.Snapshot().turboMode, TurboMode::Turbo);
    v.Update(controls, kFrame, ground);
    EXPECT_EQ(v.Snapshot().turboMode, TurboMode::Ultra);
    controls.toggleTurbo = false;
    Drive(v, ground, 5.0f, [controls](float) { return controls; });
    EXPECT_GE(v.SpeedKmh(), 390.0f);
    EXPECT_LE(v.SpeedKmh(), 405.0f);

    controls.throttle = 0.0f;
    controls.flightDescend = true;
    Drive(v, ground, 3.0f, [controls](float) { return controls; });
    EXPECT_GE(v.OriginPosition().Y, 1.8f);
    controls.toggleFlight = true;
    controls.flightDescend = false;
    v.Update(controls, kFrame, ground);
    EXPECT_FALSE(v.Snapshot().flightMode);
    EXPECT_NEAR(v.OriginPosition().Y, 0.0f, 0.1f);
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
    EXPECT_GT(distance, 32.0f);
    EXPECT_LT(distance, 42.0f);
    EXPECT_LT(t, 6.0f);
    EXPECT_NEAR(v.OriginPosition().X, start.X, 1.0f) << "braking must not pull the car sideways";
}

TEST_F(VehicleDrive, AWetRoadLengthensTheBrakingDistance)
{
    const auto stoppingDistance = [this](const float wetness) {
        Vehicle v(def, TransmissionMode::Automatic);
        v.SetRoadWetness(wetness);
        Drive(v, ground, 2.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        v.ForceForwardSpeed(Units::KmhToMs(100.0f));
        const Vector3 start = v.OriginPosition();
        float t = 0.0f;
        while (v.SpeedKmh() > 1.0f && t < 20.0f) {
            DriverControls c;
            c.brake = 1.0f;
            v.Update(c, kFrame, ground);
            t += kFrame;
        }
        return (v.OriginPosition() - start).Length();
    };
    const float dry = stoppingDistance(0.0f);
    const float wet = stoppingDistance(1.0f);
    EXPECT_GT(wet, dry * 1.20f) << "wet " << wet << " m vs dry " << dry << " m";
    EXPECT_LT(wet, dry * 1.80f) << "wet asphalt, not ice";
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

// ---- Driving feel audit (Phase 11, RQ-100): parking, reversing, cruising, climbing ----------

TEST_F(VehicleDrive, FullLockAtParkingSpeedTurnsInAPlausibleCircle)
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
    // Creep with a little throttle and full right lock; record the position when the car has
    // turned 90 and 270 degrees to measure the turning circle.
    const float yaw0 = Yaw(v);
    float turned = 0.0f, previous = yaw0;
    Vector3 at90{}, at270{};
    bool got90 = false, got270 = false;
    float maxSpeed = 0.0f;
    for (float t = 0.0f; t < 40.0f && !got270; t += kFrame) {
        DriverControls k;
        k.throttle = v.SpeedKmh() < 8.0f ? 0.25f : 0.0f;
        k.steering = 1.0f;
        v.Update(k, kFrame, ground);
        maxSpeed = std::max(maxSpeed, v.SpeedKmh());
        float delta = Yaw(v) - previous;
        while (delta > Units::DegToRad(180.0f)) delta -= Units::DegToRad(360.0f);
        while (delta < -Units::DegToRad(180.0f)) delta += Units::DegToRad(360.0f);
        turned += -delta;   // right turn = negative yaw
        previous = Yaw(v);
        if (!got90 && turned >= Units::DegToRad(90.0f)) { got90 = true; at90 = v.OriginPosition(); }
        if (!got270 && turned >= Units::DegToRad(270.0f)) { got270 = true; at270 = v.OriginPosition(); }
    }
    ASSERT_TRUE(got270) << "the car should complete three quarters of a circle within 40 s";
    EXPECT_LT(maxSpeed, 14.0f) << "a parking manoeuvre stays at walking pace";
    const float diameter = Vector3::Distance(at90, at270);   // opposite points of the circle
    EXPECT_GT(diameter, 8.0f) << "turning circle of a small hatchback (origin path; kerb to kerb ~10-11 m)";
    EXPECT_LT(diameter, 13.5f);
}

TEST_F(VehicleDrive, ReverseGearDrivesBackwardsAndSwingsTheNoseTheOtherWay)
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
    c.selector = AutomaticSelector::Reverse;
    v.Update(c, kFrame, ground);
    const Vector3 start = v.OriginPosition();
    const float yaw0 = Yaw(v);
    Drive(v, ground, 4.0f, [](float) { DriverControls k; k.throttle = 0.3f; k.steering = 0.6f; return k; });
    const Vector3 forward = v.Body().Forward();
    EXPECT_LT(Vector3::Dot(v.Body().LinearVelocity(), forward), -1.0f) << "the car moves backwards along its own axis";
    EXPECT_LT(v.SpeedKmh(), 25.0f) << "reverse is a low gear";
    EXPECT_GT(v.OriginPosition().Z - start.Z, 3.0f) << "facing -Z, reversing moves towards +Z";
    // Front wheels steered right while reversing swing the nose to the left (positive yaw).
    EXPECT_GT(Yaw(v) - yaw0, Units::DegToRad(5.0f));
}

TEST_F(VehicleDrive, HoldsFiftyOnTheFlatWithPartThrottle)
{
    Vehicle v(def, TransmissionMode::Automatic);
    DriverControls c;
    c.brake = 1.0f;
    v.Update(c, kFrame, ground);
    c.toggleEngine = true;
    v.Update(c, kFrame, ground);
    c.toggleEngine = false;
    Drive(v, ground, 3.0f, [c](float) { return c; });
    c.selector = AutomaticSelector::Drive;
    v.Update(c, kFrame, ground);
    v.ForceForwardSpeed(Units::KmhToMs(50.0f));
    // A simple speed hold: more throttle below 50, less above; the pedal should settle low.
    float pedalSum = 0.0f;
    int samples = 0;
    float pedal = 0.15f;
    for (float t = 0.0f; t < 12.0f; t += kFrame) {
        pedal = std::clamp(pedal + (50.0f - v.SpeedKmh()) * 0.004f, 0.0f, 0.6f);
        DriverControls k;
        k.throttle = pedal;
        v.Update(k, kFrame, ground);
        if (t > 6.0f) { pedalSum += pedal; ++samples; }
    }
    EXPECT_NEAR(v.SpeedKmh(), 50.0f, 4.0f);
    EXPECT_LT(pedalSum / static_cast<float>(std::max(1, samples)), 0.35f) << "50 km/h on the flat needs a light pedal";
}

TEST_F(VehicleDrive, ClimbsAnEightPercentGradeWithoutLosingMuchSpeed)
{
    // Uphill when driving towards -Z: the ground rises as z decreases.
    const FunctionGround slope([](float, float z) { return -0.08f * z; });
    Vehicle v(def, TransmissionMode::Automatic);
    DriverControls c;
    c.brake = 1.0f;
    v.Update(c, kFrame, slope);
    c.toggleEngine = true;
    v.Update(c, kFrame, slope);
    c.toggleEngine = false;
    Drive(v, slope, 3.0f, [c](float) { return c; });
    c.selector = AutomaticSelector::Drive;
    v.Update(c, kFrame, slope);
    v.ForceForwardSpeed(Units::KmhToMs(50.0f));
    Drive(v, slope, 8.0f, [](float) { DriverControls k; k.throttle = 1.0f; return k; });
    EXPECT_GT(v.SpeedKmh(), 48.0f) << "60 kW is plenty for 8 % at 50 km/h";
    EXPECT_GT(v.OriginPosition().Y, 5.0f) << "and the car has climbed";
    EXPECT_EQ(v.GetEngine().State(), EngineState::Running);
}

// Rain does not just change the picture: it changes the numbers. The wet grip factor is a single
// constant, so it is worth knowing what it actually buys -- these measurements are printed so the
// table in docs/vehicle-physics.md can be checked against the model rather than remembered.
TEST_F(VehicleDrive, WetBrakingAndCorneringAreNoticeableButControllable)
{
    const auto brakingDistance = [this](const float fromKmh, const float wetness) {
        Vehicle v(def, TransmissionMode::Automatic);
        v.SetRoadWetness(wetness);
        Drive(v, ground, 2.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
        v.ForceForwardSpeed(Units::KmhToMs(fromKmh));
        const Vector3 start = v.OriginPosition();
        float t = 0.0f;
        while (v.SpeedKmh() > 1.0f && t < 25.0f) {
            DriverControls c;
            c.brake = 1.0f;
            v.Update(c, kFrame, ground);
            t += kFrame;
        }
        return (v.OriginPosition() - start).Length();
    };

    struct Row { float kmh; float dry; float wet; };
    std::vector<Row> rows;
    for (const float kmh : {50.0f, 90.0f, 100.0f}) {
        rows.push_back(Row{kmh, brakingDistance(kmh, 0.0f), brakingDistance(kmh, 1.0f)});
    }
    for (const auto& r : rows) {
        const float g = Units::KmhToMs(r.kmh) * Units::KmhToMs(r.kmh) / (2.0f * r.dry) / 9.81f;
        std::cout << "  " << r.kmh << " -> 0 km/h: dry " << r.dry << " m (" << g << " g), wet "
                  << r.wet << " m, " << (r.wet / r.dry - 1.0f) * 100.0f << " % longer\n";
        // A small hatchback on dry asphalt stops from 100 in about 40 m, which is a shade over
        // 0.9 g. Anything beyond 1.1 g is a race car and anything under 0.6 g is a lorry.
        EXPECT_GT(g, 0.60f) << r.kmh << " km/h: dry braking is too weak";
        EXPECT_LT(g, 1.10f) << r.kmh << " km/h: dry braking is too strong for a road car";
        // Rain has to be felt but must not be ice: a fifth longer at least, not half as far again
        // plus a third.
        EXPECT_GT(r.wet, r.dry * 1.20f) << r.kmh << " km/h: rain is not noticeable";
        EXPECT_LT(r.wet, r.dry * 1.80f) << r.kmh << " km/h: wet asphalt, not ice";
    }
    // The proportional penalty must not depend on the speed: the same tyres, the same road.
    const float spread = std::fabs(rows.front().wet / rows.front().dry - rows.back().wet / rows.back().dry);
    EXPECT_LT(spread, 0.12f) << "the wet penalty should be roughly the same at every speed";

    // Cornering: the fastest speed at which the car still holds a steady steering input without
    // the front tyres ploughing. It is a comparison between dry and wet, not a skidpad figure.
    const auto corneringSpeed = [this](const float wetness) {
        float best = 0.0f;
        for (float kmh = 30.0f; kmh <= 110.0f; kmh += 5.0f) {
            Vehicle v(def, TransmissionMode::Automatic);
            v.SetRoadWetness(wetness);
            StartEngine(v, ground);
            v.ForceForwardSpeed(Units::KmhToMs(kmh));
            // Hold a constant steering angle and a trickle of throttle for three seconds.
            float peakSlip = 0.0f;
            Drive(v, ground, 3.0f, [&](float) {
                DriverControls c;
                c.steering = 0.42f;
                c.throttle = 0.18f;
                return c;
            });
            for (const auto& w : v.Snapshot().wheels) {
                peakSlip = std::max(peakSlip, std::fabs(w.slipAngle));
            }
            // Still gripping (slip angle under about 11 degrees) and not scrubbed off its speed.
            if (peakSlip < 0.20f && v.SpeedKmh() > kmh * 0.75f) best = kmh;
        }
        return best;
    };
    const float dryCorner = corneringSpeed(0.0f);
    const float wetCorner = corneringSpeed(1.0f);
    std::cout << "  steady cornering hold: dry " << dryCorner << " km/h, wet " << wetCorner << " km/h\n";
    EXPECT_GT(dryCorner, 30.0f) << "the car should hold a corner on a dry road";
    EXPECT_LE(wetCorner, dryCorner) << "rain must not make the car grip better";

    // Acceleration: rain costs traction off the line but the car still gets going.
    const auto zeroToFifty = [this](const float wetness) {
        Vehicle v(def, TransmissionMode::Automatic);
        v.SetRoadWetness(wetness);
        StartEngine(v, ground);
        float t = 0.0f;
        DriverControls c;
        c.throttle = 1.0f;
        c.selector = AutomaticSelector::Drive;
        while (v.SpeedKmh() < 50.0f && t < 30.0f) {
            v.Update(c, kFrame, ground);
            c.selector.reset();
            t += kFrame;
        }
        return t;
    };
    const float dryRun = zeroToFifty(0.0f);
    const float wetRun = zeroToFifty(1.0f);
    std::cout << "  0 -> 50 km/h: dry " << dryRun << " s, wet " << wetRun << " s\n";
    EXPECT_LT(dryRun, 12.0f) << "a 1.2 should reach 50 km/h inside twelve seconds";
    EXPECT_GE(wetRun, dryRun - 0.05f) << "rain must not make the car quicker";
    EXPECT_LT(wetRun, dryRun * 1.6f) << "rain should cost traction, not the whole launch";
}

namespace
{
    /// Starts the engine in P on the brake, then selects D and keeps the brake on.
    void ReadyAutomaticInDrive(Vehicle& vehicle, const GroundSurface& ground)
    {
        DriverControls c;
        c.brake = 1.0f;
        vehicle.Update(c, kFrame, ground);
        c.toggleEngine = true;
        vehicle.Update(c, kFrame, ground);
        c.toggleEngine = false;
        Drive(vehicle, ground, 3.0f, [c](float) { return c; });
        ASSERT_EQ(vehicle.GetEngine().State(), EngineState::Running);
        c.selector = AutomaticSelector::Drive;
        vehicle.Update(c, kFrame, ground);
        c.selector.reset();
        Drive(vehicle, ground, 1.0f, [c](float) { return c; });
    }
}

TEST_F(VehicleDrive, AutomaticWaitingOnTheBrakeInDriveKeepsIdlingAndStillCreeps)
{
    // Waiting at a junction in D must not stall the engine. The converter's creep load used to be
    // constant below idle and out-pulled the idle controller within three seconds.
    Vehicle v(def, TransmissionMode::Automatic);
    ReadyAutomaticInDrive(v, ground);
    Drive(v, ground, 30.0f, [](float) { DriverControls c; c.brake = 1.0f; return c; });
    EXPECT_EQ(v.GetEngine().State(), EngineState::Running) << "held on the brake in D for 30 s";
    EXPECT_GT(v.GetEngine().Rpm(), 650.0f) << "idle may sag under the converter's load, not collapse";
    EXPECT_LT(v.SpeedKmh(), 0.1f);

    Drive(v, ground, 3.0f, [](float) { return Idle(); });
    EXPECT_EQ(v.GetEngine().State(), EngineState::Running);
    EXPECT_GT(v.ForwardSpeedMs(), 0.3f) << "released, an automatic creeps forward";
}

TEST_F(VehicleDrive, AutomaticFullThrottleUpshiftsDoNotFlareTheEngine)
{
    Vehicle v(def, TransmissionMode::Automatic);
    ReadyAutomaticInDrive(v, ground);
    float peakWhileShifting = 0.0f;
    int shifts = 0;
    bool wasShifting = false;
    for (int i = 0; i < static_cast<int>(15.0f / kFrame); ++i) {
        DriverControls c;
        c.throttle = 1.0f;
        v.Update(c, kFrame, ground);
        const bool shifting = v.GetTransmission().IsShifting();
        if (shifting) peakWhileShifting = std::max(peakWhileShifting, v.GetEngine().Rpm());
        if (shifting && !wasShifting) ++shifts;
        wasShifting = shifting;
    }
    ASSERT_GE(shifts, 2) << "the drive should include at least two upshifts";
    EXPECT_LT(peakWhileShifting, def.engine.redlineRpm)
        << "with the load gone during a shift, full throttle must not rev the engine past redline";
}
