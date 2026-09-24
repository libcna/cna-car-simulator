#include "CarSim/Input/InputMapper.hpp"

#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace CarSim::Input
{
    using Microsoft::Xna::Framework::Input::Buttons;
    using Microsoft::Xna::Framework::Input::GamePad;
    using Microsoft::Xna::Framework::Input::GamePadDeadZone;
    using Microsoft::Xna::Framework::Input::GamePadState;
    using Microsoft::Xna::Framework::Input::GamePadType;
    using Microsoft::Xna::Framework::Input::Keyboard;
    using Microsoft::Xna::Framework::Input::KeyboardState;
    using Microsoft::Xna::Framework::Input::Keys;

    const char* ToString(const GameAction action)
    {
        switch (action) {
            case GameAction::Throttle: return "Throttle";
            case GameAction::Brake: return "Brake";
            case GameAction::SteerLeft: return "SteerLeft";
            case GameAction::SteerRight: return "SteerRight";
            case GameAction::Clutch: return "Clutch";
            case GameAction::Handbrake: return "Handbrake";
            case GameAction::Horn: return "Horn";
            case GameAction::ToggleEngine: return "ToggleEngine";
            case GameAction::ToggleTurbo: return "ToggleTurbo";
            case GameAction::ShiftUp: return "ShiftUp";
            case GameAction::ShiftDown: return "ShiftDown";
            case GameAction::GearNeutral: return "GearNeutral";
            case GameAction::GearReverse: return "GearReverse";
            case GameAction::Gear1: return "Gear1";
            case GameAction::Gear2: return "Gear2";
            case GameAction::Gear3: return "Gear3";
            case GameAction::Gear4: return "Gear4";
            case GameAction::Gear5: return "Gear5";
            case GameAction::Gear6: return "Gear6";
            case GameAction::SelectorPark: return "SelectorPark";
            case GameAction::SelectorDrive: return "SelectorDrive";
            case GameAction::ToggleTransmission: return "ToggleTransmission";
            case GameAction::ToggleDifferential: return "ToggleDifferential";
            case GameAction::ToggleAutoClutch: return "ToggleAutoClutch";
            case GameAction::CycleWipers: return "CycleWipers";
            case GameAction::IndicatorLeft: return "IndicatorLeft";
            case GameAction::IndicatorRight: return "IndicatorRight";
            case GameAction::Hazard: return "Hazard";
            case GameAction::Headlights: return "Headlights";
            case GameAction::HighBeam: return "HighBeam";
            case GameAction::ToggleCamera: return "ToggleCamera";
            case GameAction::ToggleFullscreen: return "ToggleFullscreen";
            case GameAction::ToggleMap: return "ToggleMap";
            case GameAction::ToggleExhaustSmoke: return "ToggleExhaustSmoke";
            case GameAction::ToggleFlight: return "ToggleFlight";
            case GameAction::ToggleWalk: return "ToggleWalk";
            case GameAction::ToggleRun: return "ToggleRun";
            case GameAction::ToggleHelp: return "ToggleHelp";
            case GameAction::ToggleDebug: return "ToggleDebug";
            case GameAction::Screenshot: return "Screenshot";
            case GameAction::ResetVehicle: return "ResetVehicle";
            case GameAction::ResetTrip: return "ResetTrip";
            case GameAction::Quit: return "Quit";
            case GameAction::VolumeUp: return "VolumeUp";
            case GameAction::VolumeDown: return "VolumeDown";
            case GameAction::ToggleMirror: return "ToggleMirror";
            case GameAction::ToggleHud: return "ToggleHud";
            case GameAction::TimeForward: return "TimeForward";
            case GameAction::TimeBackward: return "TimeBackward";
            case GameAction::ToggleTimeFlow: return "ToggleTimeFlow";
            case GameAction::CycleWeather: return "CycleWeather";
            case GameAction::Count: break;
        }
        return "?";
    }

    const char* Describe(const GameAction action)
    {
        switch (action) {
            case GameAction::Throttle: return "Accelerator";
            case GameAction::Brake: return "Brake";
            case GameAction::SteerLeft: return "Steer left";
            case GameAction::SteerRight: return "Steer right";
            case GameAction::Clutch: return "Clutch (manual)";
            case GameAction::Handbrake: return "Handbrake";
            case GameAction::Horn: return "Horn";
            case GameAction::ToggleEngine: return "Start / stop engine";
            case GameAction::ToggleTurbo: return "Cycle turbo: off / turbo / ultra / ultra ultra";
            case GameAction::ShiftUp: return "Gear up / selector up";
            case GameAction::ShiftDown: return "Gear down / selector down";
            case GameAction::GearNeutral: return "Neutral";
            case GameAction::GearReverse: return "Reverse";
            case GameAction::Gear1: return "1st gear";
            case GameAction::Gear2: return "2nd gear";
            case GameAction::Gear3: return "3rd gear";
            case GameAction::Gear4: return "4th gear";
            case GameAction::Gear5: return "5th gear";
            case GameAction::Gear6: return "6th gear";
            case GameAction::SelectorPark: return "Park (automatic)";
            case GameAction::SelectorDrive: return "Drive (automatic)";
            case GameAction::ToggleTransmission: return "Switch automatic / manual";
            case GameAction::ToggleDifferential: return "Differential open / limited slip";
            case GameAction::ToggleAutoClutch: return "Automatic clutch (manual) on / off";
            case GameAction::CycleWipers: return "Wipers off / interval / slow / fast";
            case GameAction::IndicatorLeft: return "Left indicator";
            case GameAction::IndicatorRight: return "Right indicator";
            case GameAction::Hazard: return "Hazard lights";
            case GameAction::Headlights: return "Headlights";
            case GameAction::HighBeam: return "Low / high beam";
            case GameAction::ToggleCamera: return "Cockpit / exterior camera";
            case GameAction::ToggleFullscreen: return "Toggle full screen";
            case GameAction::ToggleMap: return "Show / hide map";
            case GameAction::ToggleExhaustSmoke: return "Exhaust smoke on / off";
            case GameAction::ToggleFlight: return "Car / helicopter flight";
            case GameAction::ToggleWalk: return "Walk / return to car (engine off, stopped)";
            case GameAction::ToggleRun: return "Walk: toggle running";
            case GameAction::ToggleHelp: return "Help";
            case GameAction::ToggleDebug: return "Debug overlay";
            case GameAction::Screenshot: return "Screenshot";
            case GameAction::ResetVehicle: return "Recover vehicle";
            case GameAction::ResetTrip: return "Reset trip odometer";
            case GameAction::Quit: return "Quit";
            case GameAction::VolumeUp: return "Volume up";
            case GameAction::VolumeDown: return "Volume down";
            case GameAction::ToggleMirror: return "Toggle rear-view mirror";
            case GameAction::ToggleHud: return "Toggle HUD text";
            case GameAction::TimeForward: return "Clock forward one hour";
            case GameAction::TimeBackward: return "Clock back one hour";
            case GameAction::ToggleTimeFlow: return "Freeze or resume the clock";
            case GameAction::CycleWeather: return "Next weather (clear, cloud, overcast, rain, fog, snow)";
            case GameAction::Count: break;
        }
        return "";
    }

    std::vector<Binding> InputMapper::DefaultBindings()
    {
        return {
            {GameAction::Throttle, Keys::W}, {GameAction::Throttle, Keys::Up},
            {GameAction::Brake, Keys::S}, {GameAction::Brake, Keys::Down},
            {GameAction::SteerLeft, Keys::A}, {GameAction::SteerLeft, Keys::Left},
            {GameAction::SteerRight, Keys::D}, {GameAction::SteerRight, Keys::Right},
            {GameAction::Clutch, Keys::Q},
            {GameAction::Handbrake, Keys::Space},
            {GameAction::Horn, Keys::B},
            {GameAction::ToggleEngine, Keys::E},
            {GameAction::ToggleTurbo, Keys::O},
            {GameAction::ShiftUp, Keys::LeftShift}, {GameAction::ShiftUp, Keys::RightShift},
            {GameAction::ShiftDown, Keys::LeftControl}, {GameAction::ShiftDown, Keys::RightControl},
            {GameAction::GearNeutral, Keys::N},
            {GameAction::GearReverse, Keys::R},
            {GameAction::Gear1, Keys::D1}, {GameAction::Gear2, Keys::D2}, {GameAction::Gear3, Keys::D3},
            {GameAction::Gear4, Keys::D4}, {GameAction::Gear5, Keys::D5}, {GameAction::Gear6, Keys::D6},
            {GameAction::SelectorPark, Keys::P},
            {GameAction::SelectorDrive, Keys::F},
            {GameAction::ToggleTransmission, Keys::T},
            {GameAction::ToggleDifferential, Keys::U},
            {GameAction::ToggleAutoClutch, Keys::Z},
            {GameAction::CycleWipers, Keys::I},
            {GameAction::IndicatorLeft, Keys::OemComma},
            {GameAction::IndicatorRight, Keys::OemPeriod},
            {GameAction::Hazard, Keys::H},
            {GameAction::Headlights, Keys::L},
            {GameAction::HighBeam, Keys::K},
            {GameAction::ToggleCamera, Keys::C},
            {GameAction::ToggleFullscreen, Keys::F11},
            {GameAction::ToggleMap, Keys::M},
            {GameAction::ToggleExhaustSmoke, Keys::X},
            {GameAction::ToggleFlight, Keys::J},
            {GameAction::ToggleWalk, Keys::G},
            {GameAction::ToggleRun, Keys::LeftShift}, {GameAction::ToggleRun, Keys::RightShift},
            {GameAction::ToggleHelp, Keys::F1},
            {GameAction::ToggleDebug, Keys::F3},
            {GameAction::Screenshot, Keys::F12},
            {GameAction::ResetVehicle, Keys::Back},
            {GameAction::ResetTrip, Keys::F5},
            {GameAction::Quit, Keys::Escape},
            {GameAction::VolumeUp, Keys::PageUp},
            {GameAction::VolumeDown, Keys::PageDown},
            {GameAction::ToggleMirror, Keys::V},
            {GameAction::ToggleHud, Keys::Tab},
            {GameAction::TimeForward, Keys::F7},
            {GameAction::TimeBackward, Keys::F6},
            {GameAction::ToggleTimeFlow, Keys::F8},
            // Not F9 or F10: the CNA runtime keeps those for its own debug hook (F9 simulates a
            // lost graphics context, F10 restores it), which re-creates every texture.
            {GameAction::CycleWeather, Keys::F4},
        };
    }

    std::vector<PadBinding> InputMapper::DefaultPadBindings()
    {
        // Sticks and triggers drive the car directly (see UpdatePad); the buttons follow the
        // usual racing-game layout, and a wheel's paddles report as the shoulder buttons.
        return {
            {GameAction::ShiftUp, Buttons::RightShoulder},
            {GameAction::ShiftDown, Buttons::LeftShoulder},
            {GameAction::Clutch, Buttons::X},
            {GameAction::Handbrake, Buttons::B},
            {GameAction::SelectorDrive, Buttons::A},
            {GameAction::ToggleCamera, Buttons::Y},
            {GameAction::ToggleEngine, Buttons::Start},
            {GameAction::ToggleTransmission, Buttons::Back},
            {GameAction::IndicatorLeft, Buttons::DPadLeft},
            {GameAction::IndicatorRight, Buttons::DPadRight},
            {GameAction::Headlights, Buttons::DPadUp},
            {GameAction::Horn, Buttons::DPadDown},
            {GameAction::HighBeam, Buttons::LeftStick},
            {GameAction::Hazard, Buttons::RightStick},
        };
    }

    InputMapper::InputMapper()
        : bindings_(DefaultBindings()),
          padBindings_(DefaultPadBindings())
    {
    }

    void InputMapper::SetBindings(std::vector<Binding> bindings)
    {
        bindings_ = std::move(bindings);
    }

    void InputMapper::Update()
    {
        Update(Keyboard::GetState());
        using Microsoft::Xna::Framework::PlayerIndex;
        const GamePadState state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);
        bool wheel = false;
        if (state.getIsConnectedProperty()) {
            wheel = GamePad::GetCapabilities(PlayerIndex::One).getGamePadTypeProperty() == GamePadType::Wheel;
        }
        UpdatePad(state, wheel);
    }

    void InputMapper::UpdatePad(const GamePadState& state, const bool wheel)
    {
        previousPadButtons_ = pad_.buttons;
        pad_ = PadSnapshot{};
        if (!state.getIsConnectedProperty()) {
            return;
        }
        pad_.connected = true;
        pad_.wheel = wheel;
        const float x = std::clamp(state.getThumbSticksProperty().getLeftProperty().X, -1.0f, 1.0f);
        // A thumbstick has a few millimetres of travel: an exponent keeps the centre fine and
        // still reaches full lock. A wheel is already a 1:1 control.
        pad_.steering = wheel ? x : std::copysign(std::pow(std::fabs(x), 1.6f), x);
        pad_.throttle = std::clamp(state.getTriggersProperty().getRightProperty(), 0.0f, 1.0f);
        pad_.brake = std::clamp(state.getTriggersProperty().getLeftProperty(), 0.0f, 1.0f);
        static constexpr Buttons kAll[] = {
            Buttons::DPadUp, Buttons::DPadDown, Buttons::DPadLeft, Buttons::DPadRight, Buttons::Start, Buttons::Back,
            Buttons::LeftStick, Buttons::RightStick, Buttons::LeftShoulder, Buttons::RightShoulder, Buttons::A,
            Buttons::B, Buttons::X, Buttons::Y,
        };
        for (const Buttons b : kAll) {
            if (state.IsButtonDown(b)) pad_.buttons |= static_cast<std::uint32_t>(b);
        }
    }

    bool InputMapper::PadHeld(const GameAction action) const
    {
        for (const auto& b : padBindings_) {
            if (b.action == action && (pad_.buttons & static_cast<std::uint32_t>(b.button)) != 0) {
                return true;
            }
        }
        // Analog controls also count as held past half travel, so walking and flying (which
        // read actions, not pedals) answer to the controller too.
        if (pad_.connected) {
            switch (action) {
                case GameAction::Throttle: return pad_.throttle > 0.5f;
                case GameAction::Brake: return pad_.brake > 0.5f;
                case GameAction::SteerLeft: return pad_.steering < -0.5f;
                case GameAction::SteerRight: return pad_.steering > 0.5f;
                default: break;
            }
        }
        return false;
    }

    std::string InputMapper::ButtonName(const Buttons button)
    {
        switch (button) {
            case Buttons::DPadUp: return "D-pad up";
            case Buttons::DPadDown: return "D-pad down";
            case Buttons::DPadLeft: return "D-pad left";
            case Buttons::DPadRight: return "D-pad right";
            case Buttons::Start: return "Start";
            case Buttons::Back: return "Back";
            case Buttons::LeftStick: return "Left stick click";
            case Buttons::RightStick: return "Right stick click";
            case Buttons::LeftShoulder: return "LB";
            case Buttons::RightShoulder: return "RB";
            case Buttons::A: return "A";
            case Buttons::B: return "B";
            case Buttons::X: return "X";
            case Buttons::Y: return "Y";
            default: return "?";
        }
    }

    std::string InputMapper::PadButtonsFor(const GameAction action) const
    {
        std::string out;
        for (const auto& b : padBindings_) {
            if (b.action != action) continue;
            if (!out.empty()) out += " / ";
            out += ButtonName(b.button);
        }
        return out;
    }

    void InputMapper::Update(const KeyboardState& state)
    {
        previous_ = current_;
        current_ = state;
    }

    bool InputMapper::KeyHeld(const GameAction action) const
    {
        for (const auto& b : bindings_) {
            if (b.action == action && current_.IsKeyDown(b.key)) {
                return true;
            }
        }
        return false;
    }

    bool InputMapper::Held(const GameAction action) const
    {
        return KeyHeld(action) || PadHeld(action);
    }

    bool InputMapper::Pressed(const GameAction action) const
    {
        for (const auto& b : bindings_) {
            if (b.action == action && current_.IsKeyDown(b.key) && !previous_.IsKeyDown(b.key)) {
                return true;
            }
        }
        for (const auto& b : padBindings_) {
            const auto bit = static_cast<std::uint32_t>(b.button);
            if (b.action == action && (pad_.buttons & bit) != 0 && (previousPadButtons_ & bit) == 0) {
                return true;
            }
        }
        return false;
    }

    Sim::DriverControls InputMapper::BuildDriverControls(const Sim::TransmissionMode mode) const
    {
        Sim::DriverControls c;
        // The pedals and the wheel read the keys alone; the controller's analog travel is merged
        // below rather than through Held, which rounds it to on/off.
        c.throttle = KeyHeld(GameAction::Throttle) ? 1.0f : 0.0f;
        c.brake = KeyHeld(GameAction::Brake) ? 1.0f : 0.0f;
        c.clutch = Held(GameAction::Clutch) ? 1.0f : 0.0f;
        c.steering = (KeyHeld(GameAction::SteerRight) ? 1.0f : 0.0f) - (KeyHeld(GameAction::SteerLeft) ? 1.0f : 0.0f);
        if (pad_.connected) {
            // Analog controls: whichever of keyboard and controller asks for more wins.
            c.throttle = std::max(c.throttle, pad_.throttle);
            c.brake = std::max(c.brake, pad_.brake);
            if (std::fabs(pad_.steering) > std::fabs(c.steering)) c.steering = pad_.steering;
        }
        c.handbrake = Held(GameAction::Handbrake);
        c.horn = Held(GameAction::Horn);
        c.toggleEngine = Pressed(GameAction::ToggleEngine);
        c.toggleTurbo = Pressed(GameAction::ToggleTurbo);
        c.toggleFlight = Pressed(GameAction::ToggleFlight);
        c.flightClimb = Held(GameAction::Handbrake);
        c.flightDescend = Held(GameAction::Clutch);
        c.shiftUp = Pressed(GameAction::ShiftUp);
        c.shiftDown = Pressed(GameAction::ShiftDown);
        c.toggleTransmissionMode = Pressed(GameAction::ToggleTransmission);
        c.toggleDifferential = Pressed(GameAction::ToggleDifferential);
        c.toggleAutoClutch = Pressed(GameAction::ToggleAutoClutch);
        if (mode == Sim::TransmissionMode::Manual) {
            if (Pressed(GameAction::GearNeutral)) c.selectGear = 0;
            if (Pressed(GameAction::GearReverse)) c.selectGear = -1;
            const GameAction gears[] = {GameAction::Gear1, GameAction::Gear2, GameAction::Gear3,
                                        GameAction::Gear4, GameAction::Gear5, GameAction::Gear6};
            for (int i = 0; i < 6; ++i) {
                if (Pressed(gears[i])) c.selectGear = i + 1;
            }
        } else {
            if (Pressed(GameAction::SelectorPark)) c.selector = Sim::AutomaticSelector::Park;
            if (Pressed(GameAction::GearReverse)) c.selector = Sim::AutomaticSelector::Reverse;
            if (Pressed(GameAction::GearNeutral)) c.selector = Sim::AutomaticSelector::Neutral;
            if (Pressed(GameAction::SelectorDrive)) c.selector = Sim::AutomaticSelector::Drive;
        }
        if (Pressed(GameAction::IndicatorLeft)) c.indicator = Sim::IndicatorRequest::ToggleLeft;
        if (Pressed(GameAction::IndicatorRight)) c.indicator = Sim::IndicatorRequest::ToggleRight;
        if (Pressed(GameAction::Hazard)) c.indicator = Sim::IndicatorRequest::ToggleHazard;
        c.toggleHeadlights = Pressed(GameAction::Headlights);
        c.toggleHighBeam = Pressed(GameAction::HighBeam);
        c.cycleWipers = Pressed(GameAction::CycleWipers);
        c.resetTrip = Pressed(GameAction::ResetTrip);
        return c;
    }

    std::string InputMapper::KeyName(const Keys key)
    {
        switch (key) {
            case Keys::Space: return "Space";
            case Keys::LeftShift: return "Left Shift";
            case Keys::RightShift: return "Right Shift";
            case Keys::LeftControl: return "Left Ctrl";
            case Keys::RightControl: return "Right Ctrl";
            case Keys::OemComma: return ",";
            case Keys::OemPeriod: return ".";
            case Keys::Escape: return "Esc";
            case Keys::Back: return "Backspace";
            case Keys::Up: return "Up";
            case Keys::Down: return "Down";
            case Keys::Left: return "Left";
            case Keys::Right: return "Right";
            case Keys::Enter: return "Enter";
            case Keys::Tab: return "Tab";
            case Keys::PageUp: return "Page Up";
            case Keys::PageDown: return "Page Down";
            case Keys::Home: return "Home";
            case Keys::End: return "End";
            case Keys::Insert: return "Insert";
            case Keys::Delete: return "Delete";
            case Keys::OemMinus: return "-";
            case Keys::OemPlus: return "=";
            case Keys::OemSemicolon: return ";";
            case Keys::OemQuotes: return "'";
            case Keys::OemOpenBrackets: return "[";
            case Keys::OemCloseBrackets: return "]";
            case Keys::OemQuestion: return "/";
            case Keys::LeftAlt: return "Left Alt";
            case Keys::RightAlt: return "Right Alt";
            default: break;
        }
        const int code = static_cast<int>(key);
        if (code >= static_cast<int>(Keys::A) && code <= static_cast<int>(Keys::Z)) {
            return std::string(1, static_cast<char>('A' + (code - static_cast<int>(Keys::A))));
        }
        if (code >= static_cast<int>(Keys::D0) && code <= static_cast<int>(Keys::D9)) {
            return std::string(1, static_cast<char>('0' + (code - static_cast<int>(Keys::D0))));
        }
        if (code >= static_cast<int>(Keys::F1) && code <= static_cast<int>(Keys::F12)) {
            return "F" + std::to_string(code - static_cast<int>(Keys::F1) + 1);
        }
        return "key " + std::to_string(code);
    }

    std::string InputMapper::KeysFor(const GameAction action) const
    {
        std::string out;
        for (const auto& b : bindings_) {
            if (b.action == action) {
                if (!out.empty()) {
                    out += " / ";
                }
                out += KeyName(b.key);
            }
        }
        return out;
    }

    bool InputMapper::KeyFromName(const std::string& name, Keys& out)
    {
        std::string n;
        for (const char ch : name) {
            if (ch != ' ') n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (n.empty()) {
            return false;
        }
        // Try every key the mapper knows how to name.
        for (int code = 0; code < 256; ++code) {
            const Keys key = static_cast<Keys>(code);
            std::string candidate = KeyName(key);
            std::string c;
            for (const char ch : candidate) {
                if (ch != ' ') c.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            if (c == n) {
                out = key;
                return true;
            }
        }
        if (n.rfind("key", 0) == 0) {
            try {
                out = static_cast<Keys>(std::stoi(n.substr(3)));
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
        return false;
    }

    bool InputMapper::ActionFromName(const std::string& name, GameAction& out)
    {
        std::string n;
        for (const char ch : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        for (int i = 0; i < static_cast<int>(GameAction::Count); ++i) {
            const GameAction a = static_cast<GameAction>(i);
            std::string c = ToString(a);
            for (char& ch : c) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (c == n) {
                out = a;
                return true;
            }
        }
        return false;
    }

    void InputMapper::ApplyOverrides(const std::vector<std::pair<std::string, std::string>>& overrides, std::vector<std::string>& warnings)
    {
        std::array<std::size_t, static_cast<std::size_t>(GameAction::Count)> nextBinding{};
        for (const auto& [actionName, keyName] : overrides) {
            GameAction action;
            Keys key;
            if (!ActionFromName(actionName, action)) {
                warnings.push_back("bindings: unknown action '" + actionName + "'");
                continue;
            }
            if (!KeyFromName(keyName, key)) {
                warnings.push_back("bindings: unknown key '" + keyName + "' for " + actionName);
                continue;
            }
            if (key == Keys::F9 || key == Keys::F10) {
                warnings.push_back("bindings: " + keyName + " is reserved by the framework, keeping the default for " + actionName);
                continue;
            }
            // Saved profiles contain every binding, including alternates. Apply repeated
            // entries to successive slots; otherwise the last one overwrites the first.
            const std::size_t slot = nextBinding[static_cast<std::size_t>(action)]++;
            std::size_t seen = 0;
            bool replaced = false;
            for (auto& b : bindings_) {
                if (b.action == action && seen++ == slot) {
                    b.key = key;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                bindings_.push_back(Binding{action, key});
            }
        }

        // Older versions saved both alternatives as the last key (for example Right Shift /
        // Right Shift). Restore a missing default in each duplicate slot so old profiles regain
        // their left/right and WASD/arrow alternatives without discarding custom first keys.
        const auto defaults = DefaultBindings();
        for (std::size_t i = 0; i < bindings_.size(); ++i) {
            const auto& current = bindings_[i];
            const bool duplicate = std::any_of(bindings_.begin(), bindings_.begin() + static_cast<std::ptrdiff_t>(i),
                                               [&](const Binding& earlier) {
                                                   return earlier.action == current.action && earlier.key == current.key;
                                               });
            if (!duplicate) continue;
            for (const auto& fallback : defaults) {
                if (fallback.action != current.action) continue;
                const bool used = std::any_of(bindings_.begin(), bindings_.end(), [&](const Binding& existing) {
                    return existing.action == current.action && existing.key == fallback.key;
                });
                if (!used) {
                    bindings_[i].key = fallback.key;
                    break;
                }
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> InputMapper::NamedBindings() const
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& b : bindings_) {
            out.emplace_back(ToString(b.action), KeyName(b.key));
        }
        return out;
    }
}
