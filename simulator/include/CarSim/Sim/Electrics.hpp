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

    enum class WiperMode
    {
        Off,
        Intermittent,   // one wipe every few seconds
        Slow,
        Fast
    };

    [[nodiscard]] const char* ToString(WiperMode mode);

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
        /// Off -> intermittent -> slow -> fast -> off.
        void CycleWipers();

        /// Wiper blade position: 0 parked at the bottom of the screen, 1 at the far end of
        /// the sweep. A wipe in progress always finishes and parks, even when switched off.
        [[nodiscard]] float WiperPosition() const { return wiperPosition_; }
        [[nodiscard]] WiperMode Wipers() const { return wipers_; }

        static constexpr float kWipeSlowS = 1.35f;          // one up-and-back sweep
        static constexpr float kWipeFastS = 0.90f;
        static constexpr float kIntermittentPauseS = 3.5f;

        /// Self-cancelling indicator: `steerFraction` is the road-wheel angle over its lock
        /// (-1 full left .. +1 full right). Turning well into the indicated side arms the cancel
        /// cam; the wheel coming back towards straight then switches the indicator off. A lane
        /// change never turns the wheel far enough to arm it, as on a real column switch.
        void TrackSteering(float steerFraction);

        static constexpr float kSelfCancelArm = 0.30f;
        static constexpr float kSelfCancelRelease = 0.10f;

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
        bool cancelArmed_ = false;
        WiperMode wipers_ = WiperMode::Off;
        float wiperPhase_ = 0.0f;        // 0..1 through the current sweep; 0 = parked
        float wiperPause_ = 0.0f;        // intermittent: time left before the next sweep
        float wiperPosition_ = 0.0f;
    };
}
