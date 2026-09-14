#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Render;

TEST(Noise, ValueNoiseIsTileableAndBounded)
{
    for (int i = 0; i < 50; ++i) {
        const float x = static_cast<float>(i) * 0.37f;
        const float a = Noise::Value(x, 1.3f, 8, 5u);
        const float b = Noise::Value(x + 8.0f, 1.3f + 16.0f, 8, 5u);
        EXPECT_NEAR(a, b, 1e-5f) << "noise must repeat with the lattice period";
        EXPECT_GE(a, 0.0f);
        EXPECT_LE(a, 1.0f);
    }
    EXPECT_NE(Noise::Value(1.5f, 2.5f, 8, 1u), Noise::Value(1.5f, 2.5f, 8, 2u)) << "seed must change the field";
}

TEST(Image, DownsampleAveragesAndTexturesHaveExpectedTone)
{
    Image img(4, 4, Color(0, 0, 0, 255));
    img.FillRect(0, 0, 2, 4, Color(255, 255, 255, 255));
    const Image half = img.Downsampled();
    ASSERT_EQ(half.Width(), 2);
    EXPECT_EQ(half.At(0, 0).getRProperty(), 255);
    EXPECT_EQ(half.At(1, 0).getRProperty(), 0);

    const Image asphalt = Textures::Asphalt(64, 1u);
    long sum = 0;
    for (const auto& c : asphalt.Pixels()) {
        sum += c.getRProperty();
    }
    const double mean = static_cast<double>(sum) / static_cast<double>(asphalt.Pixels().size());
    EXPECT_GT(mean, 40.0);
    EXPECT_LT(mean, 130.0) << "asphalt should be a mid-dark grey";

    const Image grass = Textures::Grass(64, 2u);
    long g = 0, r = 0;
    for (const auto& c : grass.Pixels()) {
        g += c.getGProperty();
        r += c.getRProperty();
    }
    EXPECT_GT(g, r) << "grass should be greener than red";

    const Image leaves = Textures::LeafCluster(64, {0.2f, 0.4f, 0.1f}, false, 3u);
    int transparent = 0;
    for (const auto& c : leaves.Pixels()) {
        transparent += c.getAProperty() == 0 ? 1 : 0;
    }
    EXPECT_GT(transparent, 0) << "leaf clusters need transparent gaps for alpha testing";
}
