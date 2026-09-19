#include "CarSim/Core/CommandLine.hpp"

#include "CarSim/Core/Weather.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CarSim::Core
{
    namespace
    {
        bool ParseInt(std::string_view text, int& out)
        {
            int value = 0;
            const auto* first = text.data();
            const auto* last = text.data() + text.size();
            const auto result = std::from_chars(first, last, value);
            if (result.ec != std::errc{} || result.ptr != last) {
                return false;
            }
            out = value;
            return true;
        }

        /// Accepts "13", "13.5" or "hh:mm" and returns hours in [0, 24).
        bool ParseClock(std::string_view text, float& out)
        {
            const std::string value(text);
            try {
                const auto colon = value.find(':');
                float hours = 0.0f;
                if (colon == std::string::npos) {
                    size_t used = 0;
                    hours = std::stof(value, &used);
                    if (used != value.size()) {
                        return false;
                    }
                } else {
                    size_t used = 0;
                    const float h = std::stof(value.substr(0, colon), &used);
                    if (used != colon || h < 0.0f) {
                        return false;
                    }
                    const std::string minutes = value.substr(colon + 1);
                    size_t usedMinutes = 0;
                    const float m = std::stof(minutes, &usedMinutes);
                    if (usedMinutes != minutes.size() || m < 0.0f || m >= 60.0f) {
                        return false;
                    }
                    hours = h + m / 60.0f;
                }
                hours = std::fmod(hours, 24.0f);
                if (hours < 0.0f) {
                    hours += 24.0f;
                }
                out = hours;
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
    }

    CommandLineParseResult ParseCommandLine(const int argc, const char* const* argv)
    {
        CommandLineParseResult result;
        auto& options = result.options;

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];

            const auto takeValue = [&](std::string_view name) -> std::optional<std::string_view> {
                if (i + 1 >= argc) {
                    result.errors.push_back(std::string(name) + " requires a value");
                    return std::nullopt;
                }
                ++i;
                return std::string_view(argv[i]);
            };

            const auto takeInt = [&](std::string_view name, int& target, int minimum) {
                const auto value = takeValue(name);
                if (!value) {
                    return;
                }
                int parsed = 0;
                if (!ParseInt(*value, parsed) || parsed < minimum) {
                    result.errors.push_back(std::string(name) + " expects an integer >= " +
                                            std::to_string(minimum) + ", got '" + std::string(*value) + "'");
                    return;
                }
                target = parsed;
            };

            if (arg == "--help" || arg == "-h") {
                options.showHelp = true;
            } else if (arg == "--benchmark-json") {
                if (const auto value = takeValue(arg)) {
                    options.benchmark = true;
                    options.benchmarkJsonPath = std::string(*value);
                }
            } else if (arg == "--mirror-every") {
                int every = 1;
                takeInt(arg, every, 1);
                if (every >= 1) {
                    options.mirrorEvery = std::min(every, 8);
                }
            } else if (arg == "--frames") {
                int frames = 0;
                takeInt(arg, frames, 1);
                if (frames > 0) {
                    options.frames = frames;
                }
            } else if (arg == "--screenshot") {
                if (const auto value = takeValue(arg)) {
                    options.screenshotPath = std::string(*value);
                }
            } else if (arg == "--screenshot-cluster") {
                if (const auto value = takeValue(arg)) {
                    options.clusterScreenshotPath = std::string(*value);
                }
            } else if (arg == "--content") {
                if (const auto value = takeValue(arg)) {
                    options.contentDirectory = std::string(*value);
                }
            } else if (arg == "--width") {
                takeInt(arg, options.width, 64);
            } else if (arg == "--height") {
                takeInt(arg, options.height, 64);
            } else if (arg == "--fullscreen") {
                options.fullscreen = true;
            } else if (arg == "--no-audio") {
                options.noAudio = true;
            } else if (arg == "--benchmark") {
                options.benchmark = true;
            } else if (arg == "--help-overlay") {
                options.showHelpOverlay = true;
            } else if (arg == "--debug-overlay") {
                options.showDebugOverlay = true;
            } else if (arg == "--map-overlay") {
                options.showMapOverlay = true;
            } else if (arg == "--flight") {
                options.startFlight = true;
            } else if (arg == "--no-save") {
                options.noSave = true;
            } else if (arg == "--save") {
                if (const auto value = takeValue(arg)) {
                    options.savePath = std::string(*value);
                }
            } else if (arg == "--vehicle") {
                if (const auto value = takeValue(arg)) {
                    options.vehicle = std::string(*value);
                }
            } else if (arg == "--map") {
                if (const auto value = takeValue(arg)) {
                    options.map = std::string(*value);
                }
            } else if (arg == "--spawn") {
                if (const auto value = takeValue(arg)) {
                    options.spawn = std::string(*value);
                }
            } else if (arg == "--cockpit") {
                options.cockpit = true;
            } else if (arg == "--lockstep") {
                options.lockstep = true;
            } else if (arg == "--lights") {
                options.lights = true;
            } else if (arg == "--time") {
                if (const auto value = takeValue(arg)) {
                    float hours = 0.0f;
                    if (ParseClock(*value, hours)) {
                        options.timeOfDay = hours;
                    } else {
                        result.errors.push_back("--time expects hh:mm or decimal hours, got '" +
                                                std::string(*value) + "'");
                    }
                }
            } else if (arg == "--weather") {
                if (const auto value = takeValue(arg)) {
                    WeatherKind kind = WeatherKind::FewClouds;
                    if (WeatherFromName(std::string(*value), kind)) {
                        options.weather = std::string(*value);
                    } else {
                        result.errors.push_back("--weather expects clear, cloudy, overcast or rain, got '" +
                                                std::string(*value) + "'");
                    }
                }
            } else if (arg == "--time-scale") {
                if (const auto value = takeValue(arg)) {
                    try {
                        const std::string text(*value);
                        size_t used = 0;
                        const float scale = std::stof(text, &used);
                        if (used != text.size() || scale < 0.0f || scale > 3600.0f) {
                            throw std::invalid_argument("range");
                        }
                        options.timeScale = scale;
                    } catch (const std::exception&) {
                        result.errors.push_back("--time-scale expects a number in [0, 3600], got '" +
                                                std::string(*value) + "'");
                    }
                }
            } else if (arg == "--traffic-warmup") {
                int seconds = 0;
                takeInt(arg, seconds, 0);
                options.trafficWarmupSeconds = static_cast<float>(seconds);
            } else if (arg == "--auto-drive") {
                int seconds = 0;
                takeInt(arg, seconds, 0);
                options.autoDriveSeconds = static_cast<float>(seconds);
            } else if (arg == "--quality") {
                if (const auto value = takeValue(arg)) {
                    options.quality = std::string(*value);
                }
            } else if (arg == "--route") {
                if (const auto value = takeValue(arg)) {
                    options.route = std::string(*value);
                }
            } else if (arg == "--route-stay") {
                options.routeLoopStay = true;
            } else if (arg == "--chase-yaw") {
                int degrees = 0;
                takeInt(arg, degrees, -360);
                options.chaseYawDeg = static_cast<float>(degrees);
            } else if (arg == "--view") {
                // --view x y z headingDeg pitchDeg
                CommandLineOptions::FreeView view;
                float* fields[5] = {&view.x, &view.y, &view.z, &view.headingDeg, &view.pitchDeg};
                bool ok = true;
                for (float* field : fields) {
                    const auto value = takeValue(arg);
                    if (!value) {
                        ok = false;
                        break;
                    }
                    try {
                        *field = std::stof(std::string(*value));
                    } catch (const std::exception&) {
                        result.errors.push_back("--view expects five numbers: x y z headingDeg pitchDeg");
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    options.freeView = view;
                }
            } else if (arg == "--chase-distance") {
                int metres = 0;
                takeInt(arg, metres, 2);
                options.chaseDistanceM = static_cast<float>(metres);
            } else if (arg == "--eye") {
                // --eye dx dy dz yawDeg pitchDeg (cockpit camera offsets for inspection captures)
                CommandLineOptions::FreeView eye;
                float* fields[5] = {&eye.x, &eye.y, &eye.z, &eye.headingDeg, &eye.pitchDeg};
                bool ok = true;
                for (float* field : fields) {
                    const auto value = takeValue(arg);
                    if (!value) {
                        ok = false;
                        break;
                    }
                    try {
                        *field = std::stof(std::string(*value));
                    } catch (const std::exception&) {
                        result.errors.push_back("--eye expects five numbers: dx dy dz yawDeg pitchDeg");
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    options.eyeOffset = eye;
                }
            } else {
                result.errors.push_back("unknown argument '" + std::string(arg) + "'");
            }
        }
        return result;
    }

    std::string CommandLineUsage()
    {
        return
            "Usage: cna-car-simulator [options]\n"
            "\n"
            "  --width <px>          Back buffer width (default 1280)\n"
            "  --height <px>         Back buffer height (default 720)\n"
            "  --fullscreen          Start in full-screen mode\n"
            "  --no-audio            Disable the audio stream\n"
            "  --save <file>         Save file (default: $XDG_DATA_HOME/cna-car-simulator/save.json)\n"
            "  --no-save             Do not load or write the save file\n"
            "  --benchmark           Print frame-time statistics at exit (combine with --frames)\n"
            "  --benchmark-json <f>  Also write the frame statistics (per pass, scene counts) as JSON\n"
            "  --mirror-every <n>    Redraw the rear-view mirror every n frames (1 = every frame)\n"
            "  --lockstep            One simulation step per drawn frame (deterministic captures on slow renderers)\n"
            "  --traffic-warmup <s>  Simulate the traffic for s seconds before the first frame (captures)\n"
            "  --lights              Switch the headlights on at start (captures)\n"
            "  --time <hh:mm>        Clock the world starts at (also accepts decimal hours)\n"
            "  --time-scale <x>      Simulated seconds of the clock per real second (0 freezes the sky)\n"
            "  --weather <name>      clear, cloudy, overcast or rain (the weather starts settled)\n"
            "  --help-overlay        Start with the help overlay open\n"
            "  --debug-overlay       Start with the debug overlay open\n"
            "  --map-overlay         Start with the M map open\n"
            "  --flight              Start in helicopter mode\n"
            "  --content <dir>       Content root directory\n"
            "  --vehicle <name>      Vehicle definition to drive\n"
            "  --map <name>          Map to load\n"
            "  --spawn <name>        Player spawn point of the map (see traffic.json)\n"
            "  --cockpit             Start in the cockpit camera\n"
            "  --auto-drive <s>      Scripted drive: start the engine and accelerate for s seconds\n"
            "  --quality <tier>      Graphics tier: low / medium / high (default: the saved setting)\n"
            "  --route <name>        Drive a named route from the map's traffic.json and exit at its end\n"
            "  --route-stay          Keep running after the route finishes\n"
            "  --chase-yaw <deg>     Rotate the exterior camera around the car (0 = behind)\n"
            "  --chase-distance <m>  Exterior camera distance in metres\n"
            "  --eye dx dy dz yaw pitch  Cockpit camera offset (vehicle metres, degrees) for inspection\n"
            "  --view x y z hdg pitch  Fixed inspection camera (metres, degrees; heading 0 = north)\n"
            "  --frames <n>          Run n frames and exit (smoke tests)\n"
            "  --screenshot <file>   Save the last frame as PNG before exiting\n"
            "  --screenshot-cluster <file>  Save the instrument cluster texture of the last frame\n"
            "  -h, --help            Show this help\n";
    }
}
