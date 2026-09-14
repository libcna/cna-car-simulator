// Lamps, indicators and horn state.
#pragma once

#include "CarSim/Sim/DriverControls.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    enum class IndicatorMode
    {
        Off,
        Left,
        Right,
        Hazard
    };

    enum class HeadlightMode
    {
        Off,
        Low,
        High
    };

    class Electrics
    {
    public:
        explicit Electrics(const ElectricsDefinition& definition) : def_(definition) {}

        void ApplyIndicator(IndicatorRequest request);
        void ToggleHeadlights();
        void ToggleHighBeam();
        void SetHorn(bool on) { horn_ = on; }

        /// Advances the blink phase. `ignitionOn` gates every lamp except hazards.
        void Step(float dt, bool ignitionOn);

        [[nodiscard]] IndicatorMode Indicator() const { return indicator_; }
        [[nodiscard]] HeadlightMode Headlights() const { return headlights_; }
        [[nodiscard]] bool Horn() const { return horn_; }

        /// Lamp outputs (already blinking).
        [[nodiscard]] bool LeftIndicatorLit() const;
        [[nodiscard]] bool RightIndicatorLit() const;
        [[nodiscard]] bool LowBeamOn() const;
        [[nodiscard]] bool HighBeamOn() const;
        [[nodiscard]] bool BlinkPhaseOn() const { return blinkOn_; }

        /// True on the step the blink phase changed (relay click for audio).
        [[nodiscard]] bool BlinkEdge() const { return blinkEdge_; }

    private:
        const ElectricsDefinition& def_;
        IndicatorMode indicator_ = IndicatorMode::Off;
        HeadlightMode headlights_ = HeadlightMode::Off;
        bool horn_ = false;
        bool ignitionOn_ = false;
        float blinkTimer_ = 0.0f;
        bool blinkOn_ = false;
        bool blinkEdge_ = false;
    };
}
