// Centralised mapping from keyboard state to driver and application actions.
#pragma once

#include "CarSim/Sim/DriverControls.hpp"

#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <string>
#include <utility>
#include <vector>

namespace CarSim::Input
{
    enum class GameAction
    {
        Throttle, Brake, SteerLeft, SteerRight, Clutch, Handbrake, Horn,
        ToggleEngine, ToggleTurbo, ShiftUp, ShiftDown, GearNeutral, GearReverse,
        Gear1, Gear2, Gear3, Gear4, Gear5, Gear6,
        SelectorPark, SelectorDrive, ToggleTransmission,
        IndicatorLeft, IndicatorRight, Hazard, Headlights, HighBeam,
        ToggleCamera, ToggleFullscreen, ToggleMap, ToggleFlight, ToggleWalk, ToggleRun, ToggleHelp, ToggleDebug, Screenshot, ResetVehicle, ResetTrip, Quit,
        VolumeUp, VolumeDown, ToggleMirror, ToggleHud,
        TimeForward, TimeBackward, ToggleTimeFlow, CycleWeather,
        Count
    };

    [[nodiscard]] const char* ToString(GameAction action);
    [[nodiscard]] const char* Describe(GameAction action);

    struct Binding
    {
        GameAction action;
        Microsoft::Xna::Framework::Input::Keys key;
    };

    /// Polls the keyboard once per frame and answers held/pressed queries per action.
    /// Bindings are data so a settings file (or a gamepad layer) can replace them later.
    class InputMapper
    {
    public:
        InputMapper();

        [[nodiscard]] static std::vector<Binding> DefaultBindings();
        void SetBindings(std::vector<Binding> bindings);
        [[nodiscard]] const std::vector<Binding>& Bindings() const { return bindings_; }

        /// Reads the keyboard; call once per frame before querying.
        void Update();
        /// Feeds an explicit state (tests).
        void Update(const Microsoft::Xna::Framework::Input::KeyboardState& state);

        [[nodiscard]] bool Held(GameAction action) const;
        [[nodiscard]] bool Pressed(GameAction action) const;   // went down this frame

        /// Driver intent for this frame derived from the current key state, for the active mode.
        [[nodiscard]] Sim::DriverControls BuildDriverControls(Sim::TransmissionMode mode) const;

        /// Human-readable key name for the help overlay ("Left Shift", ",", "F1").
        [[nodiscard]] static std::string KeyName(Microsoft::Xna::Framework::Input::Keys key);
        /// Inverse of KeyName (case-insensitive; also accepts "key <code>"). False when unknown.
        [[nodiscard]] static bool KeyFromName(const std::string& name, Microsoft::Xna::Framework::Input::Keys& out);
        /// Action from its ToString name (case-insensitive). False when unknown.
        [[nodiscard]] static bool ActionFromName(const std::string& name, GameAction& out);
        /// Applies (action, key) overrides on top of the current bindings; unknown names are reported.
        void ApplyOverrides(const std::vector<std::pair<std::string, std::string>>& overrides, std::vector<std::string>& warnings);
        /// Current bindings as (action, key) name pairs (for saving).
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> NamedBindings() const;
        /// All keys bound to an action, joined with " / ".
        [[nodiscard]] std::string KeysFor(GameAction action) const;

    private:
        std::vector<Binding> bindings_;
        Microsoft::Xna::Framework::Input::KeyboardState current_;
        Microsoft::Xna::Framework::Input::KeyboardState previous_;
    };
}
