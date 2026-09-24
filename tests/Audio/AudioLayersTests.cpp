// Audio layer envelopes: gear-change dip, overrun burble gate, brake hiss, surface roughness.
#include "CarSim/Audio/AudioLayers.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Audio::Layers;
using CarSim::Sim::SurfaceType;

TEST(AudioLayers, ShiftDipCutsThenRecoversWithinAQuarterSecond)
{
    EXPECT_NEAR(ShiftDip(0.0f), 0.2f, 1e-5f);
    EXPECT_LT(ShiftDip(0.1f), ShiftDip(0.2f));
    EXPECT_NEAR(ShiftDip(kShiftDipSeconds), 1.0f, 1e-5f);
    EXPECT_NEAR(ShiftDip(5.0f), 1.0f, 1e-5f);
    EXPECT_NEAR(ShiftDip(-1.0f), 1.0f, 1e-5f);   // no shift yet
}

TEST(AudioLayers, OverrunBurbleFiresIrregularlyOnlyOnOverrun)
{
    int pops = 0;
    for (unsigned b = 0; b < 400; ++b) {
        const float v = OverrunBurble(b, 3200.0f, 0.0f, 0.0f, 60.0f);
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 0.22f);
        if (v > 0.0f) ++pops;
    }
    EXPECT_GT(pops, 60);    // about a third of 400 blocks
    EXPECT_LT(pops, 220);
    EXPECT_FLOAT_EQ(OverrunBurble(7u, 3200.0f, 0.5f, 0.0f, 60.0f), 0.0f);   // under load
    EXPECT_FLOAT_EQ(OverrunBurble(7u, 3200.0f, 0.0f, 0.4f, 60.0f), 0.0f);   // throttle open
    EXPECT_FLOAT_EQ(OverrunBurble(7u, 1500.0f, 0.0f, 0.0f, 60.0f), 0.0f);   // idle-ish rpm
    EXPECT_FLOAT_EQ(OverrunBurble(7u, 3200.0f, 0.0f, 0.0f, 5.0f), 0.0f);    // stopped
    // Deterministic for a block index.
    EXPECT_FLOAT_EQ(OverrunBurble(42u, 3200.0f, 0.0f, 0.0f, 60.0f), OverrunBurble(42u, 3200.0f, 0.0f, 0.0f, 60.0f));
}

TEST(AudioLayers, BrakeHissGrowsWithPedalAndSpeed)
{
    EXPECT_FLOAT_EQ(BrakeHissGain(1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(BrakeHissGain(0.0f, 80.0f), 0.0f);
    EXPECT_LT(BrakeHissGain(0.3f, 50.0f), BrakeHissGain(0.9f, 50.0f));
    EXPECT_LT(BrakeHissGain(0.9f, 20.0f), BrakeHissGain(0.9f, 50.0f));
    EXPECT_LE(BrakeHissGain(1.0f, 200.0f), 0.2f);
}

TEST(AudioLayers, SurfaceRoughnessOrdersSurfacesSensibly)
{
    EXPECT_FLOAT_EQ(SurfaceRoughness(SurfaceType::Asphalt), 1.0f);
    EXPECT_GT(SurfaceRoughness(SurfaceType::Gravel), SurfaceRoughness(SurfaceType::Grass));
    EXPECT_GT(SurfaceRoughness(SurfaceType::Grass), SurfaceRoughness(SurfaceType::Asphalt));
    EXPECT_GT(SurfaceRoughness(SurfaceType::Cobbles), SurfaceRoughness(SurfaceType::Concrete));
}

TEST(AudioLayers, RainPattersMoreInHeavyRainAndOnAMovingCar)
{
    EXPECT_FLOAT_EQ(RainDropRate(0.0f, 50.0f), 0.0f);
    EXPECT_GT(RainDropRate(0.3f, 0.0f), 10.0f);
    EXPECT_GT(RainDropRate(1.0f, 0.0f), 3.0f * RainDropRate(0.3f, 0.0f));
    EXPECT_GT(RainDropRate(1.0f, 90.0f), RainDropRate(1.0f, 0.0f));
}

TEST(AudioLayers, WipersSwishWithBladeSpeedAndSqueakWhenDry)
{
    EXPECT_FLOAT_EQ(WiperSwishGain(0.0f, 1.0f), 0.0f);
    EXPECT_GT(WiperSwishGain(2.0f, 1.0f), WiperSwishGain(0.5f, 1.0f));
    EXPECT_GT(WiperSwishGain(2.0f, 0.0f), WiperSwishGain(2.0f, 1.0f));
}

TEST(AudioLayers, PuddlesSplashOnlyOnASoakedRoadAtSpeed)
{
    EXPECT_FLOAT_EQ(SplashRate(0.2f, 80.0f), 0.0f);
    EXPECT_FLOAT_EQ(SplashRate(1.0f, 5.0f), 0.0f);
    EXPECT_GT(SplashRate(1.0f, 80.0f), SplashRate(1.0f, 30.0f));
}
