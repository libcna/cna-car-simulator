// SIM-019: constant-speed fuel consumption of the reference car in the automatic mode.
#include "CarSim/Sim/Ground.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace CarSim;

namespace
{
    /// Drives at a constant speed with a simple throttle controller and returns L/100 km over
    /// the measured distance (after settling).
    float CruiseConsumption(const float targetKmh, const float measureKm)
    {
        Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
        Sim::Vehicle vehicle(def, Sim::TransmissionMode::Automatic);
        Sim::FlatGround ground(0.0f);
        vehicle.PlaceAt(Microsoft::Xna::Framework::Vector3(0, 0, 0), 0.0f);
        Sim::DriverControls c;
        c.toggleEngine = true;
        vehicle.Update(c, 1.0f / 60.0f, ground);
        c.toggleEngine = false;
        for (int i = 0; i < 120; ++i) vehicle.Update(c, 1.0f / 60.0f, ground);   // starter, idle
        c.selector = Sim::AutomaticSelector::Drive;
        vehicle.Update(c, 1.0f / 60.0f, ground);
        c.selector.reset();
        const float target = targetKmh / 3.6f;
        vehicle.ForceForwardSpeed(target);
        float throttle = 0.25f;
        double settledStartKm = -1.0;
        float startLiters = 0.0f;
        for (int frame = 0; frame < 60 * 600; ++frame) {
            const float error = target - vehicle.ForwardSpeedMs();
            throttle = std::clamp(throttle + error * 0.02f, 0.0f, 1.0f);
            c.throttle = throttle;
            c.brake = error < -1.0f ? 0.3f : 0.0f;
            vehicle.Update(c, 1.0f / 60.0f, ground);
            const double km = vehicle.GetOdometer().TotalKm();
            if (settledStartKm < 0.0 && frame > 60 * 20) {
                settledStartKm = km;
                startLiters = vehicle.Fuel().Liters();
            }
            if (settledStartKm >= 0.0 && km - settledStartKm >= static_cast<double>(measureKm)) {
                const float used = startLiters - vehicle.Fuel().Liters();
                return used / static_cast<float>(km - settledStartKm) * 100.0f;
            }
        }
        return -1.0f;
    }
}

TEST(FuelCycle, ConstantSpeedConsumptionIsPlausible)
{
    // A 1.2 l petrol hatchback: roughly 3.5-6.5 l/100 km at 50 km/h and 4.5-8 l/100 km at 90 km/h.
    const float at50 = CruiseConsumption(50.0f, 1.5f);
    const float at90 = CruiseConsumption(90.0f, 2.5f);
    ASSERT_GT(at50, 0.0f);
    ASSERT_GT(at90, 0.0f);
    EXPECT_GT(at50, 3.0f);
    EXPECT_LT(at50, 7.0f);
    EXPECT_GT(at90, 4.0f);
    EXPECT_LT(at90, 9.0f);
    EXPECT_GT(at90, at50 * 0.9f);   // aerodynamic drag makes the faster cruise thirstier
}
