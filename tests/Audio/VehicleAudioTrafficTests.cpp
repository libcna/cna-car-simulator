// Integration of spatial traffic voices with the player vehicle's stereo mixer.
#include "CarSim/Audio/TrafficAudio.hpp"
#include "CarSim/Audio/VehicleAudio.hpp"

#include <gtest/gtest.h>
#include <algorithm>
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

TEST(TrafficAudio, PassingCarIsAudibleAboveTheLowEngineFundamental)
{
    CarSim::Audio::VehicleAudio audio(false);
    CarSim::Sim::VehicleState state;
    const Vector3 listener(0.0f, 0.0f, 0.0f);
    const Vector3 right(1.0f, 0.0f, 0.0f);
    std::vector<float> block;
    double energy = 0.0;
    double changeEnergy = 0.0;
    float previous = 0.0f;
    int samples = 0;
    for (int rendered = 0; rendered < 6 * CarSim::Audio::VehicleAudio::kSampleRate;
         rendered += CarSim::Audio::VehicleAudio::kBlockFrames) {
        const float t = static_cast<float>(rendered) / CarSim::Audio::VehicleAudio::kSampleRate;
        const TrafficSoundSource car{17, Vector3(-84.0f + t * 28.0f, 0.0f, -9.0f),
                                     Vector3(28.0f, 0.0f, 0.0f), 28.0f};
        audio.SetTrafficScene(std::span<const TrafficSoundSource>(&car, 1), listener, Vector3(0, 0, 0), right);
        audio.RenderBlock(block, state, false);
        const int frames = std::min(CarSim::Audio::VehicleAudio::kBlockFrames,
                                    6 * CarSim::Audio::VehicleAudio::kSampleRate - rendered);
        for (int i = 0; i < frames; ++i) {
            const float sample = block[static_cast<std::size_t>(i) * 2] * audio.levels.master;
            energy += static_cast<double>(sample) * static_cast<double>(sample);
            const double change = static_cast<double>(sample) - static_cast<double>(previous);
            changeEnergy += change * change;
            previous = sample;
            ++samples;
        }
    }
    const double rms = std::sqrt(energy / samples);
    const double changeRms = std::sqrt(changeEnergy / samples);
    EXPECT_GT(rms, 0.025);       // the previous nearly inaudible pass-by measured 0.016
    EXPECT_GT(changeRms, 0.005); // road and air texture reaches beyond the bass tone
    EXPECT_LT(rms, 0.10);
}
