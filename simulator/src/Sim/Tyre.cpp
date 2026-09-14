#include "CarSim/Sim/Tyre.hpp"

#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    namespace
    {
        // Magic formula y = sin(C * atan(B x - E (B x - atan(B x)))) evaluated with B chosen so
        // that the peak (y = 1) sits at x = 1. For C in (1, 2) the peak is where
        // C * atan(B') = pi/2 with B' the effective argument; solving exactly is not needed:
        // a numeric bracket over B at construction would be cleaner, but the shape is only
        // evaluated with normalised slip, so we solve for B once per call analytically:
        //   peak of sin(C atan(z)) is at atan(z) = pi / (2C)  ->  z = tan(pi / (2C)).
        // With E the argument z(x) = B x - E (B x - atan(B x)); at x = 1 we need z(1) = tan(pi/2C).
        float SolvePeakB(const float c, const float e)
        {
            const float target = std::tan(std::numbers::pi_v<float> / (2.0f * c));
            float lo = 0.1f;
            float hi = 50.0f;
            for (int i = 0; i < 40; ++i) {
                const float mid = 0.5f * (lo + hi);
                const float z = mid - e * (mid - std::atan(mid));
                if (z < target) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            return 0.5f * (lo + hi);
        }
    }

    float TyreModel::ShapeFunction(const float rho) const
    {
        const float c = std::clamp(def_.shapeFactor, 1.05f, 2.4f);
        const float e = std::clamp(def_.curvatureFactor, -2.0f, 0.999f);
        const float b = SolvePeakB(c, e);
        const float x = std::fabs(rho);
        const float bx = b * x;
        const float z = bx - e * (bx - std::atan(bx));
        return std::sin(c * std::atan(z));
    }

    float TyreModel::PeakFriction(const float loadN, const float surfaceFactor) const
    {
        const float loadRatio = def_.nominalLoadN > 1.0f ? loadN / def_.nominalLoadN : 1.0f;
        const float sensitivity = 1.0f - def_.loadSensitivity * (loadRatio - 1.0f);
        return std::max(0.2f, def_.peakFriction * std::clamp(sensitivity, 0.6f, 1.3f) * surfaceFactor);
    }

    TyreForces TyreModel::Compute(const float slipRatio, const float slipAngleRad, const float loadN,
                                  const float surfaceFactor) const
    {
        TyreForces out;
        if (loadN <= 0.0f) {
            return out;
        }
        const float peakAlpha = Units::DegToRad(def_.peakSlipAngleDeg);
        const float kappaN = slipRatio / def_.peakSlipRatio;
        const float alphaN = slipAngleRad / peakAlpha;
        const float rho = std::sqrt(kappaN * kappaN + alphaN * alphaN);
        const float mu = PeakFriction(loadN, surfaceFactor);
        out.friction = mu;
        out.combinedSlip = rho;
        if (rho < 1e-6f) {
            return out;
        }
        const float magnitude = loadN * mu * ShapeFunction(rho);
        // Longitudinal force acts with the slip ratio sign (wheel faster than road pushes forward);
        // lateral force opposes the lateral slip (contact patch moving right pushes the tyre left).
        out.longitudinal = magnitude * (kappaN / rho);
        out.lateral = -magnitude * (alphaN / rho);
        return out;
    }

    float TyreModel::LongitudinalStiffness(const float loadN, const float surfaceFactor) const
    {
        // Slope of the shape function at the origin is B*C (for the normalised curve); divide by
        // the peak slip ratio to get the slope per unit slip ratio.
        const float c = std::clamp(def_.shapeFactor, 1.05f, 2.4f);
        const float e = std::clamp(def_.curvatureFactor, -2.0f, 0.999f);
        const float b = SolvePeakB(c, e);
        const float mu = PeakFriction(std::max(loadN, 0.0f), surfaceFactor);
        return std::max(loadN, 0.0f) * mu * b * c / def_.peakSlipRatio;
    }
}
