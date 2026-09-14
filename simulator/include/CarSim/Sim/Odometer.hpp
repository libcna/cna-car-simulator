// Distance bookkeeping from ground speed (not wheel rotation).
#pragma once

namespace CarSim::Sim
{
    class Odometer
    {
    public:
        [[nodiscard]] double TotalKm() const { return totalMeters_ / 1000.0; }
        [[nodiscard]] double TripKm() const { return tripMeters_ / 1000.0; }
        [[nodiscard]] double TotalMeters() const { return totalMeters_; }

        void SetTotalKm(double km) { totalMeters_ = km * 1000.0; }
        void SetTripKm(double km) { tripMeters_ = km * 1000.0; }
        void ResetTrip() { tripMeters_ = 0.0; }

        /// Adds the distance actually travelled over the ground during a step.
        void Add(float groundSpeedMs, float dt);

    private:
        double totalMeters_ = 0.0;
        double tripMeters_ = 0.0;
    };
}
