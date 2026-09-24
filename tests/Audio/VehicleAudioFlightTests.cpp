// The helicopter path is intentionally a separate sound from the car engine.
#include "CarSim/Audio/VehicleAudio.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace CarSim;

namespace
{
    double Rms(const std::vector<float>& interleaved)
    {
        double energy = 0.0;
        for (const float sample : interleaved) energy += static_cast<double>(sample) * sample;
        return std::sqrt(energy / static_cast<double>(interleaved.size()));
    }

    double Magnitude(const std::vector<float>& mono, const float hz)
    {
        double real = 0.0, imaginary = 0.0;
        for (std::size_t i = 0; i < mono.size(); ++i) {
            const double angle = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / Audio::VehicleAudio::kSampleRate;
            real += mono[i] * std::cos(angle);
            imaginary -= mono[i] * std::sin(angle);
        }
        return std::hypot(real, imaginary) / static_cast<double>(mono.size());
    }
}

TEST(VehicleAudioFlight, RotorTakesOverFromTheCarAndFadesOnExit)
{
    Audio::VehicleAudio audio(false);
    Sim::VehicleState state;
    state.flightMode = true;
    state.engineState = Sim::EngineState::Off;
    std::vector<float> stereo;
    for (int block = 0; block < 30; ++block) audio.RenderBlock(stereo, state, false);
    EXPECT_GT(Rms(stereo), 0.04);
    for (const float sample : stereo) EXPECT_LE(std::fabs(sample), 1.0f);
    state.flightMode = false;
    for (int block = 0; block < 25; ++block) audio.RenderBlock(stereo, state, false);
    EXPECT_LT(Rms(stereo), 0.002);
}

TEST(VehicleAudioFlight, BoostModesChangeBladeCadence)
{
    const auto render = [](const Sim::TurboMode mode) {
        Audio::VehicleAudio audio(false);
        Sim::VehicleState state;
        state.flightMode = true;
        state.turboMode = mode;
        std::vector<float> stereo;
        std::vector<float> mono;
        for (int block = 0; block < 90; ++block) {
            audio.RenderBlock(stereo, state, false);
            if (block < 15) continue; // let the rotor fade in
            for (std::size_t i = 0; i < stereo.size(); i += 2) mono.push_back(stereo[i]);
        }
        return mono;
    };
    const auto normal = render(Sim::TurboMode::Off);
    const auto extreme = render(Sim::TurboMode::UltraUltra);
    EXPECT_GT(Magnitude(normal, 10.0f), Magnitude(normal, 16.0f) * 2.0);
    EXPECT_GT(Magnitude(extreme, 16.0f), Magnitude(extreme, 10.0f) * 2.0);
}
