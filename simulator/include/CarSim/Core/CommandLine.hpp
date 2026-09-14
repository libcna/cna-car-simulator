// Command-line options of the simulator executable.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace CarSim::Core
{
    /// Options accepted by the simulator executable. All fields have sensible
    /// defaults so `cna-car-simulator` with no arguments starts the game.
    struct CommandLineOptions
    {
        /// Run this many frames and then exit. std::nullopt means run until the
        /// player quits. Used by smoke tests and screenshot capture.
        std::optional<int> frames;

        /// Save the back buffer of the last frame to this PNG file before exiting.
        std::optional<std::string> screenshotPath;

        /// Content root override. When empty the executable looks for a
        /// `content/` directory next to itself, then falls back to the source tree.
        std::string contentDirectory;

        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool showHelp = false;

        /// Name of the vehicle definition to drive (overrides the saved setting).
        std::optional<std::string> vehicle;

        /// Name of the map to load (overrides the default map).
        std::optional<std::string> map;
    };

    /// Result of parsing: either options or an error message for the user.
    struct CommandLineParseResult
    {
        CommandLineOptions options;
        std::vector<std::string> errors;

        [[nodiscard]] bool ok() const { return errors.empty(); }
    };

    /// Parses `argv[1..argc)`. Unknown arguments are reported as errors rather
    /// than ignored so typos do not silently start the game with defaults.
    [[nodiscard]] CommandLineParseResult ParseCommandLine(int argc, const char* const* argv);

    /// Human-readable usage text (also shown by the in-game help).
    [[nodiscard]] std::string CommandLineUsage();
}
