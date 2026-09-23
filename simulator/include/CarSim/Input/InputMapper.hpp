// Centralised mapping from keyboard state to driver and application actions.
#pragma once

#include "CarSim/Sim/DriverControls.hpp"

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <cstdint>
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
        SelectorPark, SelectorDrive, ToggleTransmission, ToggleDifferential,
        IndicatorLeft, IndicatorRight, Hazard, Headlights, HighBeam,
        ToggleCamera, ToggleFullscreen, ToggleMap, ToggleExhaustSmoke, ToggleFlight, ToggleWalk, ToggleRun, ToggleHelp, ToggleDebug, Screenshot, ResetVehicle, ResetTrip, Quit,
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

    /// A game-controller button bound to an action (buttons are fixed, not user-rebindable).
    struct PadBinding
    {
        GameAction action;
        Microsoft::Xna::Framework::Input::Buttons button;
    };

    /// What the first game controller reported this frame, reduced to what driving needs.
    struct PadSnapshot
    {
        bool connected = false;
        bool wheel = false;              // a steering wheel: linear steering, no stick curve
        float steering = 0.0f;           // -1..1, positive = right
        float throttle = 0.0f;           // 0..1 (right trigger / accelerator pedal)
        float brake = 0.0f;              // 0..1 (left trigger / brake pedal)
        std::uint32_t buttons = 0;       // Buttons flags held
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
        /// Feeds a game-controller state (tests; Update() reads player one itself). `wheel`
        /// selects the steering-wheel profile.
        void UpdatePad(const Microsoft::Xna::Framework::Input::GamePadState& state, bool wheel);
        [[nodiscard]] const PadSnapshot& Pad() const { return pad_; }
        [[nodiscard]] static std::vector<PadBinding> DefaultPadBindings();
        /// Controller buttons bound to an action, joined with " / " (empty when none).
        [[nodiscard]] std::string PadButtonsFor(GameAction action) const;
        [[nodiscard]] static std::string ButtonName(Microsoft::Xna::Framework::Input::Buttons button);

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
        [[nodiscard]] bool KeyHeld(GameAction action) const;
        [[nodiscard]] bool PadHeld(GameAction action) const;

        std::vector<Binding> bindings_;
        std::vector<PadBinding> padBindings_;
        PadSnapshot pad_;
        std::uint32_t previousPadButtons_ = 0;
        Microsoft::Xna::Framework::Input::KeyboardState current_;
        Microsoft::Xna::Framework::Input::KeyboardState previous_;
    };
}
