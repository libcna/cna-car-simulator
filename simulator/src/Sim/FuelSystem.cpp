#include "CarSim/Sim/FuelSystem.hpp"

#include "CarSim/Sim/Engine.hpp"
#include "CarSim/Sim/Units.hpp"

#include <algorithm>

namespace CarSim::Sim
{
    FuelSystem::FuelSystem(const FuelTankDefinition& tank, const FuelConsumptionDefinition& consumption)
        : tank_(tank),
          consumption_(consumption),
          liters_(std::clamp(tank.initialLiters, 0.0f, tank.tankLiters))
    {
    }

    float FuelSystem::TripAverageLPer100Km(const float tripKm) const
    {
        if (tripKm < 0.05f) {
            return 0.0f;
        }
        return tripLiters_ / tripKm * 100.0f;
    }

    void FuelSystem::SetLiters(const float liters)
    {
        liters_ = std::clamp(liters, 0.0f, tank_.tankLiters);
    }

    float FuelSystem::MassFlowGramsPerSecond(const Engine& engine) const
    {
        if (!engine.Injecting()) {
            return 0.0f;
        }
        // Idle flow covers the friction/pumping work of a warm engine; the load-dependent part
        // follows a brake-specific-fuel-consumption map over the brake power.
        const float idleGramsPerSecond = consumption_.idleLitersPerHour * Units::kPetrolDensityKgPerL * 1000.0f / 3600.0f;
        const float load = engine.LoadFraction();
        const float bsfc = consumption_.bsfcGPerKwh.Evaluate(std::clamp(load, 0.0f, 1.0f));
        const float powerGramsPerSecond = bsfc * engine.BrakePowerKw() / 3600.0f;
        // Above idle the idle term scales with rpm because friction work grows with speed.
        const float rpmFactor = std::max(1.0f, engine.Rpm() / std::max(400.0f, engine.Definition().idleRpm));
        return idleGramsPerSecond * rpmFactor + powerGramsPerSecond;
    }

    void FuelSystem::Step(const float dt, const Engine& engine)
    {
        refilledThisStep_ = false;
        const float gramsPerSecond = MassFlowGramsPerSecond(engine);
        const float litersPerSecond = gramsPerSecond / (Units::kPetrolDensityKgPerL * 1000.0f);
        litersPerHour_ = litersPerSecond * 3600.0f;
        const float used = std::min(liters_, litersPerSecond * dt);
        liters_ -= used;
        tripLiters_ += used;

        const float refillThreshold = tank_.reserveLiters * tank_.refillAtReserveFraction;
        if (liters_ <= refillThreshold) {
            liters_ = tank_.tankLiters * tank_.refillToFraction;
            ++refillCount_;
            refilledThisStep_ = true;
        }
    }
}
