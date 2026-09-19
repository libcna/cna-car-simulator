#include "CarSim/Sim/Engine.hpp"

#include "CarSim/Sim/Units.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    const char* ToString(const EngineState state)
    {
        switch (state) {
            case EngineState::Off: return "Off";
            case EngineState::Starting: return "Starting";
            case EngineState::Running: return "Running";
            case EngineState::Stalled: return "Stalled";
        }
        return "?";
    }

    const char* ToString(const TurboMode mode)
    {
        switch (mode) {
            case TurboMode::Off: return "Off";
            case TurboMode::Turbo: return "Turbo";
            case TurboMode::Ultra: return "Ultra turbo";
        }
        return "?";
    }

    void Engine::CycleTurboMode()
    {
        switch (turboMode_) {
            case TurboMode::Off: turboMode_ = TurboMode::Turbo; break;
            case TurboMode::Turbo: turboMode_ = TurboMode::Ultra; break;
            case TurboMode::Ultra: turboMode_ = TurboMode::Off; break;
        }
    }

    float Engine::PowerMultiplier() const
    {
        switch (turboMode_) {
            case TurboMode::Off: return 1.0f;
            case TurboMode::Turbo: return 2.0f;
            case TurboMode::Ultra: return 5.0f;
        }
        return 1.0f;
    }

    Engine::Engine(const EngineDefinition& definition)
        : def_(definition),
          crankSecondsTarget_(definition.starter.crankSeconds)
    {
    }

    float Engine::Rpm() const
    {
        return Units::RadSToRpm(omega_);
    }

    void Engine::SetAngularVelocity(const float radPerS)
    {
        omega_ = std::max(0.0f, radPerS);
    }

    void Engine::SetState(const EngineState state)
    {
        if (state_ != state) {
            state_ = state;
            stateTime_ = 0.0f;
        }
    }

    void Engine::RequestStart()
    {
        if (state_ == EngineState::Off || state_ == EngineState::Stalled) {
            SetState(EngineState::Starting);
        }
    }

    void Engine::RequestStop()
    {
        if (state_ != EngineState::Off) {
            SetState(EngineState::Off);
        }
    }

    void Engine::Toggle()
    {
        if (state_ == EngineState::Running || state_ == EngineState::Starting) {
            RequestStop();
        } else {
            RequestStart();
        }
    }

    float Engine::EffectiveThrottle(const float driverThrottle) const
    {
        if (state_ != EngineState::Running) {
            return 0.0f;
        }
        const float rpm = Rpm();
        const float throttle = std::clamp(driverThrottle, 0.0f, 1.0f);

        // Idle control: opens the throttle progressively below the idle target (plus any
        // post-start flare) so the engine settles at idle under light accessory load.
        const float idleTarget = def_.idleRpm + flare_;
        const float idleBand = 300.0f;
        const float idleThrottle = std::clamp((idleTarget + 40.0f - rpm) / idleBand, 0.0f, 0.35f);
        float effective = std::max(throttle, idleThrottle);

        // Soft rev limiter: fades combustion out over the last 200 rpm before the limiter.
        const float limiterStart = def_.limiterRpm - 200.0f;
        if (rpm > limiterStart) {
            const float fade = std::clamp((def_.limiterRpm - rpm) / 200.0f, 0.0f, 1.0f);
            effective *= fade;
        }
        return effective;
    }

    float Engine::MaxTorqueAtCurrentRpm() const
    {
        return std::max(0.0f, def_.torqueCurve.Evaluate(Rpm())) * PowerMultiplier();
    }

    float Engine::CombustionTorque(const float effectiveThrottle) const
    {
        if (state_ != EngineState::Running || !fuelAvailable_) {
            return 0.0f;
        }
        return MaxTorqueAtCurrentRpm() * std::clamp(effectiveThrottle, 0.0f, 1.0f);
    }

    float Engine::FrictionTorque() const
    {
        const float rpm = Rpm();
        const auto& f = def_.frictionTorque;
        return std::max(0.0f, f[0] + f[1] * rpm + f[2] * rpm * rpm);
    }

    float Engine::StarterTorque() const
    {
        if (state_ != EngineState::Starting) {
            return 0.0f;
        }
        // The starter is a speed-limited motor: full torque at rest, none above crank speed.
        const float crankOmega = Units::RpmToRadS(def_.starter.crankRpm);
        const float ratio = std::clamp(1.0f - omega_ / (crankOmega * 1.15f), 0.0f, 1.0f);
        const float stallTorque = FrictionTorque() * 3.0f + 25.0f;
        return stallTorque * ratio;
    }

    float Engine::NetTorque(const float driverThrottle) const
    {
        const float theta = EffectiveThrottle(driverThrottle);
        const float combustion = CombustionTorque(theta);
        return combustion + StarterTorque() - EngineBrakingTorque(theta);
    }

    float Engine::EngineBrakingTorque(const float effectiveThrottle) const
    {
        // The torque curve is the published net (brake) torque, so mechanical friction is
        // already paid for at open throttle; what remains is the closed-throttle pumping and
        // friction loss that produces engine braking. It fades out as the throttle opens.
        if (omega_ <= 0.0f) {
            return 0.0f;
        }
        const float closed = state_ == EngineState::Running ? 1.0f - std::clamp(effectiveThrottle, 0.0f, 1.0f) : 1.0f;
        return FrictionTorque() * closed;
    }

    void Engine::Step(const float dt, const float driverThrottle, const bool fuelAvailable,
                      const bool integrate, const float externalLoadTorque)
    {
        fuelAvailable_ = fuelAvailable;
        stateTime_ += dt;

        switch (state_) {
            case EngineState::Starting:
                if (fuelAvailable && stateTime_ >= crankSecondsTarget_ &&
                    Rpm() >= def_.starter.crankRpm * 0.6f) {
                    SetState(EngineState::Running);
                    // Combustion catches: the crankshaft jumps to the catch speed and the idle
                    // controller aims a little above idle for a moment (the familiar start flare).
                    omega_ = std::max(omega_, Units::RpmToRadS(def_.starter.catchRpm));
                    flare_ = 450.0f;
                }
                break;
            case EngineState::Running:
                flare_ = std::max(0.0f, flare_ - dt * 600.0f);
                if (!fuelAvailable) {
                    SetState(EngineState::Stalled);
                } else if (Rpm() < def_.stallRpm && stateTime_ > 0.25f) {
                    SetState(EngineState::Stalled);
                }
                break;
            case EngineState::Off:
            case EngineState::Stalled:
                break;
        }

        if (integrate) {
            const float net = NetTorque(driverThrottle) - externalLoadTorque;
            omega_ = std::max(0.0f, omega_ + net / def_.inertiaKgM2 * dt);
        }

        RecordDelivered(EffectiveThrottle(driverThrottle));
    }

    void Engine::RecordDelivered(const float effectiveThrottle)
    {
        const float maxTorque = MaxTorqueAtCurrentRpm();
        const float combustion = CombustionTorque(effectiveThrottle);
        lastLoadFraction_ = maxTorque > 1.0f ? std::clamp(combustion / maxTorque, 0.0f, 1.0f) : 0.0f;
        lastBrakePowerKw_ = std::max(0.0f, (combustion - EngineBrakingTorque(effectiveThrottle)) * omega_) / 1000.0f;
        lastLimiterActive_ = state_ == EngineState::Running && Rpm() > def_.limiterRpm - 200.0f;
        const bool overrun = def_.fuel.overrunCutoff && effectiveThrottle <= 0.001f && Rpm() > def_.idleRpm + 600.0f;
        lastInjecting_ = state_ == EngineState::Running && fuelAvailable_ && !overrun;
    }
}
