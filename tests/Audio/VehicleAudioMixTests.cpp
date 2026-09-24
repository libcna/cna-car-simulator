#include "CarSim/Audio/VehicleAudio.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <span>
#include <vector>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector3;

TEST(VehicleAudioMix, CameraSwitchStartsAtThePreviousMixAndCrossfadesWithinTheBlock)
{
    const Audio::TrafficSoundSource passing{42, Vector3(7.0f, 0.0f, -4.0f), Vector3(0.0f, 0.0f, -12.0f),
                                            12.0f, 0.2f, false};
    Sim::VehicleState state;
    state.engineState = Sim::EngineState::Running;
    state.engineRpm = 2500.0f;
    state.engineLoad = 0.4f;
    state.throttlePedal = 0.35f;

    for (const bool initiallyInside : {false, true}) {
        Audio::VehicleAudio steady(false), switched(false);
        for (Audio::VehicleAudio* audio : {&steady, &switched}) {
            audio->SetTrafficScene(std::span<const Audio::TrafficSoundSource>(&passing, 1),
                                   Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
                                   Vector3(1.0f, 0.0f, 0.0f));
        }
        std::vector<float> unchanged, changed;
        for (int block = 0; block < 20; ++block) {
            steady.RenderBlock(unchanged, state, initiallyInside);
            switched.RenderBlock(changed, state, initiallyInside);
        }
        steady.RenderBlock(unchanged, state, initiallyInside);
        switched.RenderBlock(changed, state, !initiallyInside);
        ASSERT_EQ(changed.size(), unchanged.size());
        // At the camera edge the first sample must retain the old acoustic perspective,
        // including the traffic mix. The fade can then move within this audio block.
        EXPECT_NEAR(changed[0], unchanged[0], 1e-5f);
        EXPECT_NEAR(changed[1], unchanged[1], 1e-5f);
        double lateDifference = 0.0;
        for (std::size_t i = changed.size() / 2; i < changed.size(); ++i) {
            lateDifference += std::fabs(changed[i] - unchanged[i]);
            EXPECT_LE(std::fabs(changed[i]), 1.0f);
        }
        EXPECT_GT(lateDifference, 0.01);
    }
}
