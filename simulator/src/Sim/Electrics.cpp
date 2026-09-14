#include "CarSim/Sim/Electrics.hpp"

namespace CarSim::Sim
{
    void Electrics::ApplyIndicator(const IndicatorRequest request)
    {
        const auto restartBlink = [this] {
            blinkTimer_ = 0.0f;
            blinkOn_ = true;
            blinkEdge_ = true;
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

    void Electrics::ToggleHeadlights()
    {
        headlights_ = headlights_ == HeadlightMode::Off ? HeadlightMode::Low : HeadlightMode::Off;
    }

    void Electrics::ToggleHighBeam()
    {
        if (headlights_ == HeadlightMode::Low) {
            headlights_ = HeadlightMode::High;
        } else if (headlights_ == HeadlightMode::High) {
            headlights_ = HeadlightMode::Low;
        }
    }

    void Electrics::Step(const float dt, const bool ignitionOn)
    {
        ignitionOn_ = ignitionOn;
        blinkEdge_ = false;
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
