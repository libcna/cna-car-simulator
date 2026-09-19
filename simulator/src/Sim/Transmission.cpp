#include "CarSim/Sim/Transmission.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    const char* ToString(const AutomaticSelector selector)
    {
        switch (selector) {
            case AutomaticSelector::Park: return "P";
            case AutomaticSelector::Reverse: return "R";
            case AutomaticSelector::Neutral: return "N";
            case AutomaticSelector::Drive: return "D";
        }
        return "?";
    }

    // ---------------------------------------------------------------- Transmission (base)

    float Transmission::Ratio() const
    {
        if (gear_ > 0 && gear_ <= ForwardGearCount()) {
            return def_.ratios[static_cast<std::size_t>(gear_ - 1)];
        }
        if (gear_ == -1) {
            return -def_.reverseRatio;
        }
        return 0.0f;
    }

    std::string Transmission::DisplayLabel() const
    {
        const int shown = IsShifting() ? targetGear_ : gear_;
        if (shown == 0) {
            return "N";
        }
        if (shown < 0) {
            return "R";
        }
        return std::to_string(shown);
    }

    void Transmission::BeginShift(const int newGear)
    {
        if (newGear == gear_ && !IsShifting()) {
            return;
        }
        targetGear_ = newGear;
        gear_ = 0;
        shiftTimer_ = std::max(0.01f, def_.shiftTimeS);
    }

    void Transmission::AdvanceShift(const float dt)
    {
        if (shiftTimer_ <= 0.0f) {
            return;
        }
        shiftTimer_ -= dt;
        if (shiftTimer_ <= 0.0f) {
            shiftTimer_ = 0.0f;
            gear_ = targetGear_;
            engagedEvent_ = true;
        }
    }

    void Transmission::ClearEvents()
    {
        grindEvent_ = false;
        engagedEvent_ = false;
    }

    // ---------------------------------------------------------------- ManualTransmission

    ManualTransmission::ManualTransmission(const GearboxDefinition& definition)
        : Transmission(definition)
    {
    }

    bool ManualTransmission::ShiftAllowed(const TransmissionContext& context) const
    {
        if (context.clutchPedal >= clutchOpenPoint_) {
            return true;
        }
        // With the engine off and the car stationary the driver can move the lever freely.
        return !context.engineRunning && std::fabs(context.speedMs) < 0.3f;
    }

    void ManualTransmission::RequestShiftUp()
    {
        const int base = IsShifting() ? targetGear_ : gear_;
        requestedGear_ = std::min(base + 1, ForwardGearCount());
        hasRequest_ = true;
    }

    void ManualTransmission::RequestShiftDown()
    {
        const int base = IsShifting() ? targetGear_ : gear_;
        requestedGear_ = std::max(base - 1, -1);
        hasRequest_ = true;
    }

    void ManualTransmission::RequestGear(const int gear)
    {
        requestedGear_ = std::clamp(gear, -1, ForwardGearCount());
        hasRequest_ = true;
    }

    void ManualTransmission::Step(const TransmissionContext& context)
    {
        ClearEvents();
        if (hasRequest_) {
            hasRequest_ = false;
            const int current = IsShifting() ? targetGear_ : gear_;
            if (requestedGear_ != current) {
                if (ShiftAllowed(context)) {
                    BeginShift(requestedGear_);
                } else {
                    grindEvent_ = true;
                }
            }
        }
        AdvanceShift(context.dt);
    }

    // ---------------------------------------------------------------- AutomaticTransmission

    AutomaticTransmission::AutomaticTransmission(const GearboxDefinition& definition)
        : Transmission(definition)
    {
    }

    std::string AutomaticTransmission::DisplayLabel() const
    {
        switch (selector_) {
            case AutomaticSelector::Park: return "P";
            case AutomaticSelector::Reverse: return "R";
            case AutomaticSelector::Neutral: return "N";
            case AutomaticSelector::Drive: {
                const int shown = IsShifting() ? targetGear_ : gear_;
                return shown > 0 ? "D" + std::to_string(shown) : "D";
            }
        }
        return "?";
    }

    void AutomaticTransmission::RequestSelector(const AutomaticSelector selector, const float speedMs)
    {
        if (selector == selector_) {
            return;
        }
        const bool slow = std::fabs(speedMs) < 1.5f;
        if ((selector == AutomaticSelector::Park || selector == AutomaticSelector::Reverse) && !slow) {
            grindEvent_ = true;   // refused: lock-out above walking pace
            return;
        }
        if (selector == AutomaticSelector::Drive && speedMs < -1.5f) {
            grindEvent_ = true;
            return;
        }
        selector_ = selector;
        switch (selector_) {
            case AutomaticSelector::Park:
            case AutomaticSelector::Neutral:
                BeginShift(0);
                break;
            case AutomaticSelector::Reverse:
                BeginShift(-1);
                break;
            case AutomaticSelector::Drive:
                BeginShift(1);
                break;
        }
        sinceLastShift_ = 0.0f;
    }

    void AutomaticTransmission::SelectorUp(const float speedMs)
    {
        switch (selector_) {
            case AutomaticSelector::Drive: RequestSelector(AutomaticSelector::Neutral, speedMs); break;
            case AutomaticSelector::Neutral: RequestSelector(AutomaticSelector::Reverse, speedMs); break;
            case AutomaticSelector::Reverse: RequestSelector(AutomaticSelector::Park, speedMs); break;
            case AutomaticSelector::Park: break;
        }
    }

    void AutomaticTransmission::SelectorDown(const float speedMs)
    {
        switch (selector_) {
            case AutomaticSelector::Park: RequestSelector(AutomaticSelector::Reverse, speedMs); break;
            case AutomaticSelector::Reverse: RequestSelector(AutomaticSelector::Neutral, speedMs); break;
            case AutomaticSelector::Neutral: RequestSelector(AutomaticSelector::Drive, speedMs); break;
            case AutomaticSelector::Drive: break;
        }
    }

    float AutomaticTransmission::CouplingCapacity(const float engineRpm, const float idleRpm,
                                                  const float maxCapacity) const
    {
        // A converter absorbs torque in proportion to the square of its impeller speed, so below
        // idle the creep load falls away as the engine slows. A constant creep load instead
        // out-pulled the idle controller and stalled a car held on the brake in D within seconds.
        if (engineRpm < idleRpm) {
            const float r = std::max(0.0f, engineRpm) / std::max(1.0f, idleRpm);
            return std::min(maxCapacity, def_.automatic.creepTorqueNm * r * r);
        }
        const float slipBand = std::max(50.0f, def_.automatic.lockupSlipRpm);
        const float t = std::clamp((engineRpm - idleRpm) / slipBand, 0.0f, 1.0f);
        return std::min(maxCapacity, def_.automatic.creepTorqueNm + maxCapacity * t * t);
    }

    int AutomaticTransmission::DecideGear(const TransmissionContext& context) const
    {
        const int current = IsShifting() ? targetGear_ : gear_;
        if (selector_ != AutomaticSelector::Drive || current < 1) {
            return current;
        }
        const float throttle = std::clamp(context.throttle, 0.0f, 1.0f);
        const float up = def_.automatic.upshiftRpm.Evaluate(throttle);
        const float down = def_.automatic.downshiftRpm.Evaluate(throttle);
        const auto& ratios = def_.ratios;
        const int top = ForwardGearCount();

        if (current < top && context.engineRpm > up) {
            return current + 1;
        }
        if (current > 1 && context.engineRpm < down) {
            // The lower gear must not land so high that the box would immediately shift back up.
            const float currentRatio = ratios[static_cast<std::size_t>(current - 1)] *
                                       (current == top ? context.topGearRatioFactor : 1.0f);
            const float after = context.engineRpm * ratios[static_cast<std::size_t>(current - 2)] / currentRatio;
            if (after < up - 300.0f) {
                return current - 1;
            }
        }
        return current;
    }

    void AutomaticTransmission::Step(const TransmissionContext& context)
    {
        ClearEvents();
        sinceLastShift_ += context.dt;
        if (selector_ == AutomaticSelector::Drive && !IsShifting() && gear_ >= 1 &&
            sinceLastShift_ >= def_.automatic.minShiftIntervalS) {
            const int wanted = DecideGear(context);
            if (wanted != gear_) {
                BeginShift(wanted);
                sinceLastShift_ = 0.0f;
            }
        }
        AdvanceShift(context.dt);
    }

    std::unique_ptr<Transmission> MakeTransmission(const GearboxDefinition& definition, const TransmissionMode mode)
    {
        if (mode == TransmissionMode::Automatic) {
            return std::make_unique<AutomaticTransmission>(definition);
        }
        return std::make_unique<ManualTransmission>(definition);
    }
}
