// Body damage from collisions: dents pressed into the body where it was hit, and lamps broken by
// hard impacts. Pure data in the vehicle's body frame; the renderer deforms the model from it.
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

namespace CarSim::Sim
{
    struct Dent
    {
        Microsoft::Xna::Framework::Vector3 centre{};      // body frame, on the skin
        Microsoft::Xna::Framework::Vector3 direction{};   // unit, pointing into the body
        float radius = 0.3f;                              // metres of skin affected
        float depth = 0.02f;                              // metres pushed in at the centre
    };

    class VehicleDamage
    {
    public:
        static constexpr int kMaxDents = 32;
        static constexpr float kMinSpeedMs = 2.5f;        // below this a knock leaves no mark
        static constexpr float kMaxDepthM = 0.16f;
        static constexpr float kLampBreakSpeedMs = 6.0f;

        /// Records an impact at `point` (body frame) with the surface normal `inward` pointing
        /// into the body, at `closingSpeed`. `frontZ` and `rearZ` are the body's ends (front is
        /// -Z). Returns true when it left a mark.
        bool AddImpact(const Microsoft::Xna::Framework::Vector3& point, const Microsoft::Xna::Framework::Vector3& inward,
                       float closingSpeed, float frontZ, float rearZ);
        void Repair();

        [[nodiscard]] const std::vector<Dent>& Dents() const { return dents_; }
        [[nodiscard]] bool HeadlampsBroken() const { return headlampsBroken_; }
        [[nodiscard]] bool TailLampsBroken() const { return tailLampsBroken_; }
        /// Bumped on every change, so the renderer rebuilds the deformed body only then.
        [[nodiscard]] int Version() const { return version_; }
        /// Sum of dent depths, metres (a rough "how bent is it").
        [[nodiscard]] float Severity() const;

        /// How far a body-frame point is pushed in by all dents (the renderer's deformation).
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Displacement(const Microsoft::Xna::Framework::Vector3& point) const;

    private:
        std::vector<Dent> dents_;
        bool headlampsBroken_ = false;
        bool tailLampsBroken_ = false;
        int version_ = 0;
    };
}
