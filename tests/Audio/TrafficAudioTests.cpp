#include "CarSim/Audio/TrafficAudio.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using CarSim::Audio::TrafficAudio;
using CarSim::Audio::TrafficSoundSource;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    std::pair<float, float> RenderEnergy(TrafficAudio& audio, const int frames = 44100)
    {
        std::vector<float> stereo(static_cast<std::size_t>(frames) * 2, 0.0f);
        audio.Render(stereo.data(), frames);
        double left = 0.0;
        double right = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double l = stereo[static_cast<std::size_t>(i) * 2];
            const double r = stereo[static_cast<std::size_t>(i) * 2 + 1];
            EXPECT_TRUE(std::isfinite(l));
            EXPECT_TRUE(std::isfinite(r));
            left += l * l;
            right += r * r;
        }
        return {static_cast<float>(std::sqrt(left / frames)), static_cast<float>(std::sqrt(right / frames))};
    }
}

TEST(TrafficAudio, NearbyCarPansAndFadesWithDistance)
{
    const Vector3 listener(0.0f, 0.0f, 0.0f);
    const Vector3 still(0.0f, 0.0f, 0.0f);
    const Vector3 right(1.0f, 0.0f, 0.0f);
    TrafficSoundSource car{7, Vector3(8.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -12.0f), 12.0f, 0.5f, false};
    TrafficAudio near;
    near.SetScene(std::span<const TrafficSoundSource>(&car, 1), listener, still, right);
    const auto [nearLeft, nearRight] = RenderEnergy(near);
    EXPECT_GT(nearRight, nearLeft * 5.0f);
    car.position = Vector3(70.0f, 0.0f, 0.0f);
    TrafficAudio far;
    far.SetScene(std::span<const TrafficSoundSource>(&car, 1), listener, still, right);
    const auto [farLeft, farRight] = RenderEnergy(far);
    EXPECT_GT(nearRight, farRight * 5.0f);
    EXPECT_LE(farLeft, nearLeft);

    near.SetScene({}, listener, still, right);
    RenderEnergy(near);
    EXPECT_EQ(near.ActiveVoices(), 0);
}

TEST(TrafficAudio, PrioritisesSixSourcesAndKeepsTheMixBounded)
{
    std::vector<TrafficSoundSource> cars;
    for (int i = 0; i < 20; ++i) {
        cars.push_back(TrafficSoundSource{i + 1, Vector3(4.0f + i * 3.0f, 0.0f, -5.0f),
                                         Vector3(0.0f, 0.0f, -12.0f), 12.0f, 0.0f, i % 4 == 0});
    }
    TrafficAudio audio;
    audio.SetScene(cars, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(audio.ActiveVoices(), 6);
    const auto [left, right] = RenderEnergy(audio);
    EXPECT_GT(right, left);
    EXPECT_LT(right, 0.3f);
}
