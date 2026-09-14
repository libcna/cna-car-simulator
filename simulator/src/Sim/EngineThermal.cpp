#include "CarSim/Sim/EngineThermal.hpp"

#include "CarSim/Sim/Engine.hpp"
#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    EngineThermal::EngineThermal(const ThermalDefinition& definition)
        : def_(definition),
          coolantC_(definition.ambientC)
    {
    }

    void EngineThermal::Step(const float dt, const Engine& engine, const float fuelGramsPerSecond, const float speedMs)
    {
        // Heat input: a fraction of the fuel's chemical power reaches the coolant.
        const float fuelPowerW = fuelGramsPerSecond * Units::kPetrolEnergyMjPerKg * 1000.0f;   // g/s * MJ/kg = kW
        float heatInW = fuelPowerW * def_.heatFractionOfFuel;
        if (engine.IsRunning()) {
            heatInW += 300.0f;   // friction heat of a turning engine even at closed throttle
        }

        // Heat removal: the thermostat opens progressively around thermostatOpenC and lets the
        // radiator work; ram air adds with speed; a stopped engine cools slowly by convection.
        const float above = coolantC_ - def_.ambientC;
        const float open = std::clamp((coolantC_ - def_.thermostatOpenC + 2.0f) / 6.0f, 0.0f, 1.0f);
        float conductanceWPerK = def_.offCoolingWPerK;
        if (engine.IsRunning() || engine.IsCranking()) {
            const float radiator = def_.radiatorClosedWPerK + open * (def_.radiatorWPerK - def_.radiatorClosedWPerK);
            conductanceWPerK = radiator + open * def_.airflowWPerKPerMs * std::fabs(speedMs);
            // Electric fan holds the temperature near the operating point when standing still.
            if (coolantC_ > def_.operatingC + 5.0f) {
                conductanceWPerK += def_.radiatorWPerK * 0.5f;
            }
        }
        const float heatOutW = conductanceWPerK * above;

        const float capacityJPerK = std::max(1.0f, def_.heatCapacityKjPerK) * 1000.0f;
        coolantC_ += (heatInW - heatOutW) / capacityJPerK * dt;
        coolantC_ = std::clamp(coolantC_, def_.ambientC - 5.0f, 150.0f);
    }
}
