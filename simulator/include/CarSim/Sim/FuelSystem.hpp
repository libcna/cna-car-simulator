// Fuel tank, consumption and the configurable reserve/automatic refill rule.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    class Engine;

    class FuelSystem
    {
    public:
        FuelSystem(const FuelTankDefinition& tank, const FuelConsumptionDefinition& consumption);

        [[nodiscard]] float Liters() const { return liters_; }
        [[nodiscard]] float Fraction() const { return liters_ / tank_.tankLiters; }
        [[nodiscard]] bool ReserveWarning() const { return liters_ < tank_.reserveLiters; }
        [[nodiscard]] bool HasFuel() const { return liters_ > 0.001f; }

        /// Instantaneous consumption in litres per hour (0 when the engine is off).
        [[nodiscard]] float LitersPerHour() const { return litersPerHour_; }

        /// Litres consumed since the last trip reset and the matching distance-based average.
        [[nodiscard]] float TripLiters() const { return tripLiters_; }
        [[nodiscard]] float TripAverageLPer100Km(float tripKm) const;

        /// Number of automatic refills that happened (for tests, logging and the dashboard).
        [[nodiscard]] int RefillCount() const { return refillCount_; }
        /// True on the step an automatic refill occurred.
        [[nodiscard]] bool RefilledThisStep() const { return refilledThisStep_; }

        void SetLiters(float liters);
        void ResetTrip() { tripLiters_ = 0.0f; }

        /// Advances consumption for `dt` seconds using the engine's last-step load/power.
        void Step(float dt, const Engine& engine);

        /// Fuel mass flow in g/s for the engine's current state (exposed for tests).
        [[nodiscard]] float MassFlowGramsPerSecond(const Engine& engine) const;

    private:
        const FuelTankDefinition& tank_;
        const FuelConsumptionDefinition& consumption_;
        float liters_;
        float litersPerHour_ = 0.0f;
        float tripLiters_ = 0.0f;
        int refillCount_ = 0;
        bool refilledThisStep_ = false;
    };
}
