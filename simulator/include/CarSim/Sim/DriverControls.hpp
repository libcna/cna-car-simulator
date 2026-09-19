// Driver intent for one frame, produced by the input mapper (or by scripted tests).
#pragma once

#include "CarSim/Sim/Transmission.hpp"

#include <optional>

namespace CarSim::Sim
{
    enum class IndicatorRequest
    {
        None,
        ToggleLeft,
        ToggleRight,
        ToggleHazard,
        Cancel
    };

    struct DriverControls
    {
        // Continuous inputs, 0..1 (pedals) and -1..1 (steering, positive = right).
        float throttle = 0.0f;
        float brake = 0.0f;
        float clutch = 0.0f;
        float steering = 0.0f;
        bool handbrake = false;
        bool horn = false;

        // Discrete requests, consumed once per frame.
        bool toggleEngine = false;
        bool toggleTurbo = false;
        bool shiftUp = false;
        bool shiftDown = false;
        std::optional<int> selectGear;                 // manual: -1, 0, 1..N
        std::optional<AutomaticSelector> selector;     // automatic
        bool toggleTransmissionMode = false;
        IndicatorRequest indicator = IndicatorRequest::None;
        bool toggleHeadlights = false;
        bool toggleHighBeam = false;
        bool resetTrip = false;
    };
}
