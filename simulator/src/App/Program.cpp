#include "CarSim/App/SimulatorGame.hpp"
#include "CarSim/Core/CommandLine.hpp"
#include "CarSim/Core/Version.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

int main(int argc, char* argv[])
{
#if defined(__EMSCRIPTEN__)
    // SDL's Web Audio backend hands the browser twice this many frames per callback. Its default
    // of 1024 leaves ~43 ms, which one slow frame on the main thread already exceeds. Set before
    // the game opens the audio device; an explicit value in the environment still wins.
    setenv("SDL_AUDIO_DEVICE_SAMPLE_FRAMES", "2048", 0);
#endif
    const auto parsed = CarSim::Core::ParseCommandLine(argc, argv);
    if (!parsed.ok()) {
        for (const auto& error : parsed.errors) {
            std::cerr << "cna-car-simulator: " << error << "\n";
        }
        std::cerr << "\n" << CarSim::Core::CommandLineUsage();
        return 2;
    }
    if (parsed.options.showHelp) {
        std::cout << CarSim::Core::ProductName() << " " << CarSim::Core::VersionString() << "\n\n"
                  << CarSim::Core::CommandLineUsage();
        return 0;
    }

    try {
        // Heap-allocated and intentionally kept alive for the whole process, matching the
        // lifetime CNA documents for its game loop on every platform.
        auto game = std::make_unique<CarSim::App::SimulatorGame>(parsed.options);
        game->Run();
        (void)game.release();
    } catch (const std::exception& error) {
        std::cerr << "cna-car-simulator: fatal error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
