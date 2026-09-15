// The solar model and the day/night palette of the lighting rig.
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    LightingRig At(const float hours)
    {
        LightingRig rig;
        rig.SetTimeOfDay(hours);
        return rig;
    }
}

TEST(LightingRig, SunRisesInTheEastCulminatesAtNoonAndSetsInTheWest)
{
    EXPECT_GT(At(13.0f).SunElevationDeg(), At(10.0f).SunElevationDeg());
    EXPECT_GT(At(13.0f).SunElevationDeg(), At(16.0f).SunElevationDeg());
    EXPECT_NEAR(At(13.0f).SunElevationDeg(), 90.0f - 49.8f + 20.0f, 0.5f);   // lat 49.8, declination 20
    // Morning sun in the east (azimuth below 180), evening sun in the west.
    EXPECT_LT(At(7.0f).SunAzimuthDeg(), 170.0f);
    EXPECT_GT(At(19.0f).SunAzimuthDeg(), 190.0f);
    // The light travels away from the sun: in the morning it points west (-x).
    EXPECT_LT(At(7.0f).sunDirection.X, 0.0f);
    EXPECT_GT(At(19.0f).sunDirection.X, 0.0f);
}

TEST(LightingRig, TheSunIsUpDuringTheDayAndDownAtNight)
{
    EXPECT_GT(At(6.0f).SunElevationDeg(), 0.0f);
    EXPECT_GT(At(20.0f).SunElevationDeg(), 0.0f);
    EXPECT_LT(At(4.0f).SunElevationDeg(), 0.0f);
    EXPECT_LT(At(22.0f).SunElevationDeg(), 0.0f);
    EXPECT_LT(At(1.0f).SunElevationDeg(), -10.0f);
    EXPECT_FALSE(At(13.0f).IsNight());
    EXPECT_TRUE(At(1.0f).IsNight());
    // Hours wrap.
    EXPECT_NEAR(At(25.0f).SunElevationDeg(), At(1.0f).SunElevationDeg(), 1e-3f);
}

TEST(LightingRig, NightIsDarkAndSunsetIsWarm)
{
    const LightingRig noon = At(13.0f);
    const LightingRig dusk = At(20.5f);
    const LightingRig night = At(1.0f);
    const Vector3 up(0.0f, 1.0f, 0.0f);
    EXPECT_GT(noon.Irradiance(up).Y, 4.0f * dusk.Irradiance(up).Y);
    EXPECT_GT(dusk.Irradiance(up).Y, night.Irradiance(up).Y);
    EXPECT_LT(night.Irradiance(up).Y, 0.12f);
    // Low sun is warm: more red than blue, unlike the neutral noon light.
    EXPECT_GT(dusk.sunColor.X / std::max(0.001f, dusk.sunColor.Z), noon.sunColor.X / std::max(0.001f, noon.sunColor.Z));
    // The sky follows: a warm horizon at dusk, a dark one at night.
    EXPECT_GT(dusk.horizonColor.X, dusk.horizonColor.Z);
    EXPECT_LT(night.horizonColor.X, 0.1f);
    EXPECT_LT(night.zenithColor.Y, noon.zenithColor.Y);
}

TEST(LightingRig, BakedLightingScaleFollowsTheDay)
{
    const LightingRig reference = LightingRig::BakeReference();
    // At the reference time nothing is scaled.
    EXPECT_NEAR(reference.BakedLightingScale(reference).Y, 1.0f, 1e-4f);
    const Vector3 noon = At(13.0f).BakedLightingScale(reference);
    const Vector3 night = At(1.0f).BakedLightingScale(reference);
    EXPECT_GT(noon.Y, 1.0f);
    EXPECT_LT(night.Y, 0.15f);
    EXPECT_GT(night.Y, 0.0f);
    // The scale falls monotonically from noon through the evening into the night. (It stays
    // slightly blue at dusk: a horizontal surface then sees mostly skylight, while the warm
    // colour of the low sun shows on vertical faces and in the sky itself.)
    const Vector3 evening = At(18.0f).BakedLightingScale(reference);
    const Vector3 dusk = At(20.3f).BakedLightingScale(reference);
    EXPECT_GT(noon.Y, evening.Y);
    EXPECT_GT(evening.Y, dusk.Y);
    EXPECT_GT(dusk.Y, night.Y);
}

TEST(LightingRig, MoonlightReplacesTheSunAtNight)
{
    const LightingRig night = At(0.0f);
    // The key light comes from above the horizon (a moon), not from under the ground.
    EXPECT_LT(night.sunDirection.Y, 0.0f);
    EXPECT_LT(night.sunColor.X, 0.12f);
    EXPECT_GT(night.sunColor.Z, night.sunColor.X);   // cold
}
