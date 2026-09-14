// Friction clutch: pedal position to torque capacity.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    class Clutch
    {
    public:
        explicit Clutch(const ClutchDefinition& definition) : def_(definition) {}

        /// Torque the clutch can transmit for a pedal position (1 = fully pressed, 0 = released).
        /// Fully released transmits maxTorque; fully pressed transmits nothing; in between the
        /// capacity follows a smooth engagement curve between engageEnd and engageStart.
        [[nodiscard]] float Capacity(float pedal01) const;

        /// Engagement fraction 0..1 for a pedal position (1 = fully engaged).
        [[nodiscard]] float Engagement(float pedal01) const;

        /// Inverse of Capacity(): the pedal position at which the clutch transmits `torqueNm`
        /// (clamped to the engagement band). Used by the driver-style release logic.
        [[nodiscard]] float PedalForCapacity(float torqueNm) const;

        [[nodiscard]] const ClutchDefinition& Definition() const { return def_; }

    private:
        const ClutchDefinition& def_;
    };
}
