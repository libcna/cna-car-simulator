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

        /// Save the instrument cluster render target of the last frame to this PNG file.
        std::optional<std::string> clusterScreenshotPath;

        /// Content root override. When empty the executable looks for a
        /// `content/` directory next to itself, then falls back to the source tree.
        std::string contentDirectory;

        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool showHelp = false;
        bool noAudio = false;   // skip the audio stream (headless runs, tests)
        bool benchmark = false;         // collect frame statistics and print them at exit
        std::optional<std::string> benchmarkJsonPath;   // also write the statistics as JSON to this file
        std::optional<int> mirrorEvery;  // redraw the mirror every n frames (overrides the saved setting)
        bool noMirror = false;            // disable all mirror passes for controlled captures
        bool noWingMirrors = false;        // keep the rear view but skip the two wing mirrors
        bool noWingVisibilityCull = false; // replay the earlier wing policy for a controlled A/B
        std::optional<int> mirrorWidth;   // rear-view target width; height keeps its normal aspect ratio
        std::optional<float> mirrorDistanceM; // rear-view draw distance, independent of the quality tier
        bool lockstep = false;          // exactly one 1/60 s simulation step per drawn frame (deterministic captures)
        float trafficWarmupSeconds = 0.0f;   // simulate the traffic this long before the first frame (captures)
        bool lights = false;                 // switch the headlights on at start (captures)
        std::optional<float> timeOfDay;      // clock in hours (--time 21:30 or --time 21.5)
        std::optional<float> timeScale;      // simulated seconds per real second (0 freezes the sky)
        std::optional<std::string> weather;  // clear / cloudy / overcast / rain
        int wiperSteps = 0;                  // --wipers: 1 intermittent, 2 slow, 3 fast
        std::optional<std::string> quality;  // graphics tier: low / medium / high
        bool showHelpOverlay = false;   // start with the F1 help overlay open (captures)
        bool showDebugOverlay = false;  // start with the F3 debug overlay open
        bool showMapOverlay = false;    // start with the M map open (captures)
        bool startFlight = false;       // start as a helicopter (captures)
        bool startWalking = false;      // enter walking mode at the spawn (captures/benchmarks)

        /// Save file override; empty = DefaultSavePath(). `--no-save` disables loading and saving.
        std::string savePath;
        bool noSave = false;

        /// Name of the vehicle definition to drive (overrides the saved setting).
        std::optional<std::string> vehicle;

        /// Name of the map to load (overrides the default map); "none" selects the flat proving ground.
        std::optional<std::string> map;

        /// Name of the player spawn point inside the map (default: the first one).
        std::optional<std::string> spawn;

        /// Start in the cockpit camera instead of the exterior camera.
        bool cockpit = false;

        /// Scripted driving for headless captures: seconds of throttle to apply after starting.
        std::optional<float> autoDriveSeconds;

        /// Drive a named route from the map's traffic.json with the autopilot, then exit. The
        /// car is driven over the ground by the ordinary physics; nothing is teleported. This is
        /// what the benchmark scenarios and the validation drive use.
        std::optional<std::string> route;
        /// Keep running after the route finishes instead of exiting (for watching it).
        bool routeLoopStay = false;

        /// Exterior camera framing overrides for screenshots (degrees around the car, metres).
        std::optional<float> chaseYawDeg;
        std::optional<float> chaseDistanceM;

        /// Fixed free camera for inspection captures: position, heading (deg, 0 = north) and pitch (deg, up positive).
        struct FreeView
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float headingDeg = 0.0f;
            float pitchDeg = 0.0f;
        };
        std::optional<FreeView> freeView;

        /// Cockpit camera inspection: eye offset in the vehicle frame (metres) and a yaw offset (degrees, positive = look left).
        std::optional<FreeView> eyeOffset;   // x, y, z used; headingDeg = yaw offset, pitchDeg = pitch offset
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
