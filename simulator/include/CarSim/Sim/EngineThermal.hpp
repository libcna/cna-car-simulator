// Lumped coolant temperature model.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    class Engine;
    class FuelSystem;

    class EngineThermal
    {
    public:
        explicit EngineThermal(const ThermalDefinition& definition);

        [[nodiscard]] float CoolantC() const { return coolantC_; }
        [[nodiscard]] bool Warning() const { return coolantC_ >= def_.warningC; }
        [[nodiscard]] bool ThermostatOpen() const { return coolantC_ >= def_.thermostatOpenC; }
        [[nodiscard]] bool AtOperatingTemperature() const { return coolantC_ >= def_.operatingC - 5.0f; }

        void SetCoolantC(float celsius) { coolantC_ = celsius; }

        /// `fuelGramsPerSecond` is the current fuel mass flow (heat input), `speedMs` the vehicle
        /// speed (ram-air cooling).
        void Step(float dt, const Engine& engine, float fuelGramsPerSecond, float speedMs);

    private:
        const ThermalDefinition& def_;
        float coolantC_;
    };
}
