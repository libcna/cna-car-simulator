// Internal-combustion engine model: state machine, torque production, friction, starter.
#pragma once

#include "CarSim/Sim/VehicleDefinition.hpp"

namespace CarSim::Sim
{
    enum class EngineState
    {
        Off,        // ignition off, crankshaft coasting to rest
        Starting,   // starter motor cranking; catches after crankSeconds when fuel is available
        Running,    // combustion sustains rotation
        Stalled     // combustion stopped because rpm collapsed; needs a restart
    };

    [[nodiscard]] const char* ToString(EngineState state);

    enum class TurboMode { Off, Turbo, Ultra };

    [[nodiscard]] const char* ToString(TurboMode mode);

    /// Engine model shared by the player and traffic vehicles. The engine owns its angular
    /// velocity; the vehicle's driveline may overwrite it while the clutch is locked.
    class Engine
    {
    public:
        explicit Engine(const EngineDefinition& definition);

        [[nodiscard]] const EngineDefinition& Definition() const { return def_; }
        [[nodiscard]] EngineState State() const { return state_; }
        [[nodiscard]] bool IsRunning() const { return state_ == EngineState::Running; }
        [[nodiscard]] bool IsCranking() const { return state_ == EngineState::Starting; }
        [[nodiscard]] bool IgnitionOn() const { return state_ == EngineState::Running || state_ == EngineState::Starting; }
        [[nodiscard]] TurboMode TurboSetting() const { return turboMode_; }
        void SetTurboMode(TurboMode mode) { turboMode_ = mode; }
        void CycleTurboMode();
        [[nodiscard]] float PowerMultiplier() const;

        [[nodiscard]] float Rpm() const;
        [[nodiscard]] float AngularVelocity() const { return omega_; }
        void SetAngularVelocity(float radPerS);

        /// Driver requests: start (from Off/Stalled), stop (from any state), or toggle.
        void RequestStart();
        void RequestStop();
        void Toggle();

        /// Throttle actually applied after idle control, limiter and state gating.
        /// `driverThrottle` in 0..1.
        [[nodiscard]] float EffectiveThrottle(float driverThrottle) const;

        /// Wide-open torque at the current rpm (Nm).
        [[nodiscard]] float MaxTorqueAtCurrentRpm() const;

        /// Combustion torque produced for an effective throttle (Nm, >= 0). Zero unless running.
        [[nodiscard]] float CombustionTorque(float effectiveThrottle) const;

        /// Closed-throttle friction and pumping losses at the current rpm (Nm, >= 0).
        [[nodiscard]] float FrictionTorque() const;

        /// Engine-braking torque actually opposing rotation for an effective throttle: the full
        /// friction torque at closed throttle, fading to zero at wide-open throttle (the torque
        /// curve already accounts for friction there).
        [[nodiscard]] float EngineBrakingTorque(float effectiveThrottle) const;

        /// Starter torque while cranking (Nm), zero otherwise.
        [[nodiscard]] float StarterTorque() const;

        /// Net crankshaft torque (combustion + starter - friction) for the given driver throttle.
        [[nodiscard]] float NetTorque(float driverThrottle) const;

        [[nodiscard]] float Inertia() const { return def_.inertiaKgM2; }

        /// Load fraction 0..1 used by fuel and thermal models: combustion torque relative to the
        /// wide-open torque at the current rpm.
        [[nodiscard]] float LoadFraction() const { return lastLoadFraction_; }

        /// Brake power delivered at the crankshaft during the last step (kW, >= 0).
        [[nodiscard]] float BrakePowerKw() const { return lastBrakePowerKw_; }

        /// Fuel is being injected (false during overrun cut-off, when off, or while cranking without fuel).
        [[nodiscard]] bool Injecting() const { return lastInjecting_; }

        /// Rev limiter active during the last step.
        [[nodiscard]] bool LimiterActive() const { return lastLimiterActive_; }

        /// Advances the state machine and, when `integrate` is true, integrates the free
        /// crankshaft against `externalLoadTorque` (positive = resisting). When the driveline has
        /// locked the engine to the wheels the caller passes `integrate = false` and sets the
        /// angular velocity itself.
        void Step(float dt, float driverThrottle, bool fuelAvailable, bool integrate, float externalLoadTorque);

        /// Records the torque the engine delivered to the driveline during this step, for load,
        /// power and fuel bookkeeping. Called by the vehicle after the coupling is resolved.
        void RecordDelivered(float effectiveThrottle);

        /// Seconds spent in the current state.
        [[nodiscard]] float StateTime() const { return stateTime_; }

    private:
        void SetState(EngineState state);

        const EngineDefinition& def_;
        EngineState state_ = EngineState::Off;
        float omega_ = 0.0f;              // crankshaft angular velocity (rad/s)
        float stateTime_ = 0.0f;
        float crankSecondsTarget_ = 0.8f;
        float flare_ = 0.0f;              // extra idle target right after start (rpm)
        bool fuelAvailable_ = true;
        TurboMode turboMode_ = TurboMode::Off;
        float lastLoadFraction_ = 0.0f;
        float lastBrakePowerKw_ = 0.0f;
        bool lastInjecting_ = false;
        bool lastLimiterActive_ = false;
    };
}
