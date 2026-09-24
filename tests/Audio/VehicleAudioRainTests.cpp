// Integrated vehicle mixer weather regression.
#include "CarSim/Audio/VehicleAudio.hpp"

#include <gtest/gtest.h>
#include <algorithm>

#include <cmath>
#include <vector>

TEST(VehicleAudioRain, RainWipersAndPuddlesRenderCleanly)
{
    CarSim::Audio::VehicleAudio audio(false);   // no device: render only
    audio.SetWeather(1.0f, 1.0f);
    CarSim::Sim::VehicleState state;
    state.speedKmh = 70.0f;
    state.engineRpm = 2500.0f;
    state.engineState = CarSim::Sim::EngineState::Running;
    for (auto& w : state.wheels) w.grounded = true;
    std::vector<float> stereo;
    float peak = 0.0f;
    double energy = 0.0;
    for (int block = 0; block < 200; ++block) {
        state.wiperPosition = 0.5f - 0.5f * std::cos(static_cast<float>(block) * 0.1f);
        audio.RenderBlock(stereo, state, block % 2 == 0);
        for (const float s : stereo) {
            ASSERT_TRUE(std::isfinite(s));
            peak = std::max(peak, std::fabs(s));
            energy += static_cast<double>(s) * s;
        }
    }
    EXPECT_LE(peak, 1.0f);
    EXPECT_GT(energy, 0.0);
}
