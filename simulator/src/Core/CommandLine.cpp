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
            "  --frames <n>          Run n frames and exit (smoke tests)\n"
            "  --screenshot <file>   Save the last frame as PNG before exiting\n"
            "  -h, --help            Show this help\n";
    }
}
