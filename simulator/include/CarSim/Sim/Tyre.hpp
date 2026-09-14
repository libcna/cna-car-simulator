// Tyre force model: simplified magic formula with normalised combined slip and load sensitivity.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    struct TyreForces
    {
        float longitudinal = 0.0f;   // N, along the wheel heading (+ forward)
        float lateral = 0.0f;        // N, along the wheel's right axis (+ right)
        float combinedSlip = 0.0f;   // normalised slip magnitude (1 = peak)
        float friction = 0.0f;       // peak friction coefficient used
    };

    class TyreModel
    {
    public:
        explicit TyreModel(const TyreDefinition& definition) : def_(definition) {}

        /// Normalised force shape: 0 at rho = 0, 1 at rho = 1 (peak), settling towards the
        /// sliding level for large rho. Uses the magic-formula shape with B chosen so the peak
        /// sits at rho = 1.
        [[nodiscard]] float ShapeFunction(float rho) const;

        /// Effective peak friction for a vertical load and a surface factor.
        [[nodiscard]] float PeakFriction(float loadN, float surfaceFactor) const;

        /// Forces for a slip ratio (dimensionless), slip angle (radians, positive when the
        /// contact patch moves to the right relative to the heading), vertical load and
        /// surface friction factor.
        [[nodiscard]] TyreForces Compute(float slipRatio, float slipAngleRad, float loadN, float surfaceFactor) const;

        /// Longitudinal stiffness dFx/d(slip ratio) at zero slip (N per unit slip) for the
        /// implicit wheel-spin integration.
        [[nodiscard]] float LongitudinalStiffness(float loadN, float surfaceFactor) const;

        [[nodiscard]] const TyreDefinition& Definition() const { return def_; }

    private:
        const TyreDefinition& def_;
    };
}
