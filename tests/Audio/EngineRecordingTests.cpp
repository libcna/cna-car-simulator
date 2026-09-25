#include "CarSim/Audio/EngineRecording.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace CarSim;

TEST(EngineRecording, AcceptedIdleLoopKeepsTheJoinWithinOrdinarySampleChanges)
{
    Audio::EngineRecording recording;
    ASSERT_TRUE(recording.LoadWav(std::string(CARSIM_TEST_CONTENT_DIR) + "/audio/honda-civic-2012-start-idle.wav"));
    Audio::EngineSoundInput input;
    input.state = Audio::EngineSoundState::Starting;
    input.rpm = 850.0f;
    std::vector<float> block(1024 * 2);
    std::vector<float> changes;
    changes.reserve(10 * 44100);
    float previous = 0.0f;
    float seamMax = 0.0f;
    for (int rendered = 0; rendered < 10 * 44100; rendered += 1024) {
        if (rendered >= 44100) input.state = Audio::EngineSoundState::Running;
        recording.Render(block.data(), 1024, input);
        for (int i = 0; i < 1024; ++i) {
            const int frame = rendered + i;
            if (frame >= 10 * 44100) break;
            const float current = block[static_cast<std::size_t>(i) * 2];
            const float delta = std::fabs(current - previous);
            if (frame >= 8 * 44100 - 50 && frame <= 8 * 44100 + 50) seamMax = std::max(seamMax, delta);
            if (frame > 44100) changes.push_back(delta);
            previous = current;
        }
    }
    ASSERT_FALSE(changes.empty());
    std::sort(changes.begin(), changes.end());
    const float highOrdinaryChange = changes[changes.size() * 999 / 1000];
    EXPECT_GT(highOrdinaryChange, 0.0f);
    EXPECT_LT(seamMax, highOrdinaryChange * 2.0f);
}

TEST(EngineRecording, HigherRpmFadesTheIdleLayerForTheExistingDynamicEngine)
{
    EXPECT_FLOAT_EQ(Audio::EngineRecording::IdleShare(850.0f), 1.0f);
    EXPECT_GT(Audio::EngineRecording::IdleShare(1900.0f), 0.0f);
    EXPECT_LT(Audio::EngineRecording::IdleShare(1900.0f), 1.0f);
    EXPECT_FLOAT_EQ(Audio::EngineRecording::IdleShare(3000.0f), 0.0f);
}

TEST(EngineRecording, SteadyLoadRecordingLoopsWithoutAJoinClickAndFadesWithRpm)
{
    Audio::EngineRecording recording;
    ASSERT_TRUE(recording.LoadLoadWav(std::string(CARSIM_TEST_CONTENT_DIR) + "/audio/mini-cooper-s-load.wav"));
    EXPECT_FLOAT_EQ(Audio::EngineRecording::LoadShare(850.0f, 1.0f), 0.0f);
    EXPECT_GT(Audio::EngineRecording::LoadShare(3500.0f, 1.0f),
              Audio::EngineRecording::LoadShare(3500.0f, 0.0f));
    Audio::EngineSoundInput input;
    input.state = Audio::EngineSoundState::Running;
    input.rpm = 3500.0f;
    input.load = 1.0f;
    std::vector<float> block(1024 * 2);
    std::vector<float> changes;
    changes.reserve(9 * 44100);
    float previous = 0.0f;
    float seamMax = 0.0f;
    for (int rendered = 0; rendered < 9 * 44100; rendered += 1024) {
        std::fill(block.begin(), block.end(), 0.0f);
        recording.RenderLoad(block.data(), 1024, input);
        for (int i = 0; i < 1024 && rendered + i < 9 * 44100; ++i) {
            const int frame = rendered + i;
            const float current = block[static_cast<std::size_t>(i) * 2];
            const float delta = std::fabs(current - previous);
            if (frame >= 4 * 44100 - 50 && frame <= 4 * 44100 + 50) seamMax = std::max(seamMax, delta);
            if (frame > 44100) changes.push_back(delta);
            previous = current;
        }
    }
    std::sort(changes.begin(), changes.end());
    const float highOrdinaryChange = changes[changes.size() * 999 / 1000];
    EXPECT_LT(seamMax, highOrdinaryChange * 2.0f);
}
