#include "CarSim/Core/CommandLine.hpp"

#include <charconv>
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
            } else if (arg == "--auto-drive") {
                int seconds = 0;
                takeInt(arg, seconds, 0);
                options.autoDriveSeconds = static_cast<float>(seconds);
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
            "  --content <dir>       Content root directory\n"
            "  --vehicle <name>      Vehicle definition to drive\n"
            "  --map <name>          Map to load\n"
            "  --spawn <name>        Player spawn point of the map (see traffic.json)\n"
            "  --cockpit             Start in the cockpit camera\n"
            "  --auto-drive <s>      Scripted drive: start the engine and accelerate for s seconds\n"
            "  --chase-yaw <deg>     Rotate the exterior camera around the car (0 = behind)\n"
            "  --chase-distance <m>  Exterior camera distance in metres\n"
            "  --view x y z hdg pitch  Fixed inspection camera (metres, degrees; heading 0 = north)\n"
            "  --frames <n>          Run n frames and exit (smoke tests)\n"
            "  --screenshot <file>   Save the last frame as PNG before exiting\n"
            "  -h, --help            Show this help\n";
    }
}
