#include "CarSim/Input/InputMapper.hpp"

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

namespace CarSim::Input
{
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
            case GameAction::IndicatorLeft: return "IndicatorLeft";
            case GameAction::IndicatorRight: return "IndicatorRight";
            case GameAction::Hazard: return "Hazard";
            case GameAction::Headlights: return "Headlights";
            case GameAction::HighBeam: return "HighBeam";
            case GameAction::ToggleCamera: return "ToggleCamera";
            case GameAction::ToggleFullscreen: return "ToggleFullscreen";
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
            case GameAction::IndicatorLeft: return "Left indicator";
            case GameAction::IndicatorRight: return "Right indicator";
            case GameAction::Hazard: return "Hazard lights";
            case GameAction::Headlights: return "Headlights";
            case GameAction::HighBeam: return "High beam";
            case GameAction::ToggleCamera: return "Cockpit / exterior camera";
            case GameAction::ToggleFullscreen: return "Toggle full screen";
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
            case GameAction::CycleWeather: return "Next weather (clear, cloud, overcast, rain)";
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
            {GameAction::ShiftUp, Keys::LeftShift}, {GameAction::ShiftUp, Keys::RightShift},
            {GameAction::ShiftDown, Keys::LeftControl}, {GameAction::ShiftDown, Keys::RightControl},
            {GameAction::GearNeutral, Keys::N},
            {GameAction::GearReverse, Keys::R},
            {GameAction::Gear1, Keys::D1}, {GameAction::Gear2, Keys::D2}, {GameAction::Gear3, Keys::D3},
            {GameAction::Gear4, Keys::D4}, {GameAction::Gear5, Keys::D5}, {GameAction::Gear6, Keys::D6},
            {GameAction::SelectorPark, Keys::P},
            {GameAction::SelectorDrive, Keys::F},
            {GameAction::ToggleTransmission, Keys::T},
            {GameAction::IndicatorLeft, Keys::OemComma},
            {GameAction::IndicatorRight, Keys::OemPeriod},
            {GameAction::Hazard, Keys::H},
            {GameAction::Headlights, Keys::L},
            {GameAction::HighBeam, Keys::K},
            {GameAction::ToggleCamera, Keys::C},
            {GameAction::ToggleFullscreen, Keys::F11},
            {GameAction::ToggleHelp, Keys::F1},
            {GameAction::ToggleDebug, Keys::F3},
            {GameAction::Screenshot, Keys::F12},
            {GameAction::ResetVehicle, Keys::Back},
            {GameAction::ResetTrip, Keys::F5},
            {GameAction::Quit, Keys::Escape},
            {GameAction::VolumeUp, Keys::PageUp},
            {GameAction::VolumeDown, Keys::PageDown},
            {GameAction::ToggleMirror, Keys::M},
            {GameAction::ToggleHud, Keys::Tab},
            {GameAction::TimeForward, Keys::F7},
            {GameAction::TimeBackward, Keys::F6},
            {GameAction::ToggleTimeFlow, Keys::F8},
            {GameAction::CycleWeather, Keys::F9},
        };
    }

    InputMapper::InputMapper()
        : bindings_(DefaultBindings())
    {
    }

    void InputMapper::SetBindings(std::vector<Binding> bindings)
    {
        bindings_ = std::move(bindings);
    }

    void InputMapper::Update()
    {
        Update(Keyboard::GetState());
    }

    void InputMapper::Update(const KeyboardState& state)
    {
        previous_ = current_;
        current_ = state;
    }

    bool InputMapper::Held(const GameAction action) const
    {
        for (const auto& b : bindings_) {
            if (b.action == action && current_.IsKeyDown(b.key)) {
                return true;
            }
        }
        return false;
    }

    bool InputMapper::Pressed(const GameAction action) const
    {
        for (const auto& b : bindings_) {
            if (b.action == action && current_.IsKeyDown(b.key) && !previous_.IsKeyDown(b.key)) {
                return true;
            }
        }
        return false;
    }

    Sim::DriverControls InputMapper::BuildDriverControls(const Sim::TransmissionMode mode) const
    {
        Sim::DriverControls c;
        c.throttle = Held(GameAction::Throttle) ? 1.0f : 0.0f;
        c.brake = Held(GameAction::Brake) ? 1.0f : 0.0f;
        c.clutch = Held(GameAction::Clutch) ? 1.0f : 0.0f;
        c.steering = (Held(GameAction::SteerRight) ? 1.0f : 0.0f) - (Held(GameAction::SteerLeft) ? 1.0f : 0.0f);
        c.handbrake = Held(GameAction::Handbrake);
        c.horn = Held(GameAction::Horn);
        c.toggleEngine = Pressed(GameAction::ToggleEngine);
        c.shiftUp = Pressed(GameAction::ShiftUp);
        c.shiftDown = Pressed(GameAction::ShiftDown);
        c.toggleTransmissionMode = Pressed(GameAction::ToggleTransmission);
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
            // Replace the first binding of the action (keeps alternates such as arrow keys).
            bool replaced = false;
            for (auto& b : bindings_) {
                if (b.action == action && !replaced) {
                    b.key = key;
                    replaced = true;
                }
            }
            if (!replaced) {
                bindings_.push_back(Binding{action, key});
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
