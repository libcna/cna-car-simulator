// Integration of spatial traffic voices with the player vehicle's stereo mixer.
#include "CarSim/Audio/TrafficAudio.hpp"
#include "CarSim/Audio/VehicleAudio.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using CarSim::Audio::TrafficSoundSource;
using Microsoft::Xna::Framework::Vector3;

TEST(TrafficAudio, VehicleMixerAttenuatesNearbyTrafficInsideTheCabin)
{
    TrafficSoundSource car{12, Vector3(7.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -10.0f), 10.0f, 0.0f, false};
    const auto render = [&](const bool cockpit) {
        CarSim::Audio::VehicleAudio audio(false);
        audio.SetTrafficScene(std::span<const TrafficSoundSource>(&car, 1), Vector3(0.0f, 0.0f, 0.0f),
                              Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
        CarSim::Sim::VehicleState state;
        std::vector<float> block;
        for (int i = 0; i < 43; ++i) audio.RenderBlock(block, state, cockpit);
        double energy = 0.0;
        for (std::size_t i = 1; i < block.size(); i += 2) {
            EXPECT_LE(std::fabs(block[i]), 1.0f);
            const double sample = block[i];
            energy += sample * sample;
        }
        return static_cast<float>(std::sqrt(energy / (block.size() / 2)));
    };
    const float outside = render(false);
    const float inside = render(true);
    EXPECT_GT(outside, 0.005f);
    EXPECT_LT(inside, outside * 0.4f);
}
