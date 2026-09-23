#include "CarSim/Sim/Electrics.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Sim
{
    void Electrics::ApplyIndicator(const IndicatorRequest request)
    {
        const auto restartBlink = [this] {
            blinkTimer_ = 0.0f;
            blinkOn_ = true;
            blinkEdge_ = true;
            cancelArmed_ = false;
        };
        switch (request) {
            case IndicatorRequest::None:
                break;
            case IndicatorRequest::ToggleLeft:
                indicator_ = indicator_ == IndicatorMode::Left ? IndicatorMode::Off : IndicatorMode::Left;
                if (indicator_ != IndicatorMode::Off) restartBlink();
                break;
            case IndicatorRequest::ToggleRight:
                indicator_ = indicator_ == IndicatorMode::Right ? IndicatorMode::Off : IndicatorMode::Right;
                if (indicator_ != IndicatorMode::Off) restartBlink();
                break;
            case IndicatorRequest::ToggleHazard:
                indicator_ = indicator_ == IndicatorMode::Hazard ? IndicatorMode::Off : IndicatorMode::Hazard;
                if (indicator_ != IndicatorMode::Off) restartBlink();
                break;
            case IndicatorRequest::Cancel:
                indicator_ = IndicatorMode::Off;
                break;
        }
    }

    void Electrics::TrackSteering(const float steerFraction)
    {
        float into = 0.0f;   // how far the wheel is turned towards the indicated side
        if (indicator_ == IndicatorMode::Left) {
            into = -steerFraction;
        } else if (indicator_ == IndicatorMode::Right) {
            into = steerFraction;
        } else {
            cancelArmed_ = false;
            return;
        }
        if (into >= kSelfCancelArm) {
            cancelArmed_ = true;
        } else if (cancelArmed_ && into <= kSelfCancelRelease) {
            indicator_ = IndicatorMode::Off;
            cancelArmed_ = false;
        }
    }

    const char* ToString(const WiperMode mode)
    {
        switch (mode) {
            case WiperMode::Off: return "off";
            case WiperMode::Intermittent: return "intermittent";
            case WiperMode::Slow: return "slow";
            case WiperMode::Fast: return "fast";
        }
        return "off";
    }

    void Electrics::CycleWipers()
    {
        switch (wipers_) {
            case WiperMode::Off: wipers_ = WiperMode::Intermittent; break;
            case WiperMode::Intermittent: wipers_ = WiperMode::Slow; break;
            case WiperMode::Slow: wipers_ = WiperMode::Fast; break;
            case WiperMode::Fast: wipers_ = WiperMode::Off; break;
        }
        wiperPause_ = 0.0f;   // a new setting starts with a wipe
    }

    void Electrics::ToggleHeadlights()
    {
        headlights_ = headlights_ == HeadlightMode::Off ? HeadlightMode::Low : HeadlightMode::Off;
    }

    void Electrics::ToggleHighBeam()
    {
        if (headlights_ == HeadlightMode::Off || headlights_ == HeadlightMode::Low) {
            headlights_ = HeadlightMode::High;
        } else if (headlights_ == HeadlightMode::High) {
            headlights_ = HeadlightMode::Low;
        }
    }

    void Electrics::Step(const float dt, const bool ignitionOn)
    {
        ignitionOn_ = ignitionOn;
        blinkEdge_ = false;

        // Wipers: a sweep starts only with the ignition on and the switch on; once started it
        // runs to the end and parks.
        const bool wanted = ignitionOn && wipers_ != WiperMode::Off;
        if (wiperPhase_ <= 0.0f && wanted) {
            if (wipers_ == WiperMode::Intermittent && wiperPause_ > 0.0f) {
                wiperPause_ -= dt;
            } else {
                wiperPhase_ = 1e-4f;
            }
        }
        if (wiperPhase_ > 0.0f) {
            const float period = wipers_ == WiperMode::Fast ? kWipeFastS : kWipeSlowS;
            wiperPhase_ += dt / period;
            if (wiperPhase_ >= 1.0f) {
                wiperPhase_ = 0.0f;
                wiperPause_ = kIntermittentPauseS;
            }
        }
        // Up and back: a smooth cosine, so the blade slows at both ends of its travel.
        wiperPosition_ = wiperPhase_ > 0.0f ? 0.5f - 0.5f * std::cos(wiperPhase_ * 6.2831853f) : 0.0f;

        if (indicator_ == IndicatorMode::Off) {
            blinkTimer_ = 0.0f;
            blinkOn_ = false;
            return;
        }
        blinkTimer_ += dt;
        const float half = 0.5f * def_.indicatorPeriodS;
        while (blinkTimer_ >= half) {
            blinkTimer_ -= half;
            blinkOn_ = !blinkOn_;
            blinkEdge_ = true;
        }
    }

    bool Electrics::LeftIndicatorLit() const
    {
        const bool active = indicator_ == IndicatorMode::Left || indicator_ == IndicatorMode::Hazard;
        const bool powered = ignitionOn_ || indicator_ == IndicatorMode::Hazard;
        return active && powered && blinkOn_;
    }

    bool Electrics::RightIndicatorLit() const
    {
        const bool active = indicator_ == IndicatorMode::Right || indicator_ == IndicatorMode::Hazard;
        const bool powered = ignitionOn_ || indicator_ == IndicatorMode::Hazard;
        return active && powered && blinkOn_;
    }

    bool Electrics::LowBeamOn() const
    {
        return ignitionOn_ && headlights_ != HeadlightMode::Off;
    }

    bool Electrics::HighBeamOn() const
    {
        return ignitionOn_ && headlights_ == HeadlightMode::High;
    }
}
