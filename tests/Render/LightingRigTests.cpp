// The solar model and the day/night palette of the lighting rig.
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

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
    EXPECT_LT(night.Irradiance(up).Y, 0.25f);   // moonlight: readable, an order below noon
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
    EXPECT_LT(night.Y, 0.20f);
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

// The whole day, minute by minute, in every weather. "Time interpolation should feel continuous"
// is a property, so it is checked as one: no colour the rig produces, and nothing derived from
// it -- the lamp factor, the baked-lighting scale, the fog -- may jump between two consecutive
// minutes. A discontinuity here is a visible pop in the sky or a flash on the ground at some hour
// nobody happens to screenshot.
TEST(LightingRig, TheWholeDayIsContinuousInEveryWeather)
{
    struct Sample
    {
        Vector3 sun, ambient, fill, bounce, fog, zenith, horizon;
        float lamp, baked, fogStart, fogEnd, elevation;
    };
    const LightingRig bakeReference = LightingRig::BakeReference();
    const auto sampleAt = [&bakeReference](LightingRig& rig, const float hours) {
        rig.SetTimeOfDay(hours);
        Sample s;
        s.sun = rig.sunColor;
        s.ambient = rig.skyAmbient;
        s.fill = rig.skyFillColor;
        s.bounce = rig.groundBounceColor;
        s.fog = rig.fogColor;
        s.zenith = rig.zenithColor;
        s.horizon = rig.horizonColor;
        s.lamp = rig.LampFactor();
        s.baked = rig.BakedLightingScale(bakeReference).X;
        s.fogStart = rig.fogStart;
        s.fogEnd = rig.fogEnd;
        s.elevation = rig.SunElevationDeg();
        return s;
    };

    struct Weather { const char* name; float cover; float rain; };
    for (const Weather& weather : {Weather{"clear", 0.0f, 0.0f}, Weather{"scattered", 0.35f, 0.0f},
                                   Weather{"overcast", 0.95f, 0.0f}, Weather{"rain", 1.0f, 1.0f}}) {
        LightingRig rig;
        rig.SetWeather(weather.cover, weather.rain);
        Sample previous = sampleAt(rig, 0.0f);
        float previousElevation = previous.elevation;
        float worstColour = 0.0f;
        float worstHour = 0.0f;
        const char* worstWhat = "";
        float worstScalar = 0.0f;
        float worstScalarHour = 0.0f;
        const char* worstScalarWhat = "";
        // Sampled the way the game applies it: the palette is re-applied when the sun has moved
        // RefreshStepDeg, so that is the step a player actually sees. Walking the clock ten times
        // finer than that makes sure no step is missed.
        for (int tick = 1; tick <= 24 * 60 * 10; ++tick) {
            const float hours = static_cast<float>(tick) / 600.0f;
            rig.SetTimeOfDay(hours);
            if (std::fabs(rig.SunElevationDeg() - previousElevation) < LightingRig::RefreshStepDeg(rig.SunElevationDeg())) {
                continue;
            }
            const Sample now = sampleAt(rig, hours);
            previousElevation = now.elevation;
            const auto note = [&](const float delta, const char* what) {
                if (delta > worstColour) {
                    worstColour = delta;
                    worstHour = hours;
                    worstWhat = what;
                }
            };
            note((now.sun - previous.sun).Length(), "sun colour");
            note((now.ambient - previous.ambient).Length(), "sky ambient");
            note((now.fill - previous.fill).Length(), "sky fill");
            note((now.bounce - previous.bounce).Length(), "ground bounce");
            note((now.fog - previous.fog).Length(), "fog colour");
            note((now.zenith - previous.zenith).Length(), "zenith");
            note((now.horizon - previous.horizon).Length(), "horizon");
            if (std::fabs(now.lamp - previous.lamp) > worstScalar) {
                worstScalar = std::fabs(now.lamp - previous.lamp);
                worstScalarHour = hours;
                worstScalarWhat = "lamp factor";
            }
            if (std::fabs(now.baked - previous.baked) > worstScalar) {
                worstScalar = std::fabs(now.baked - previous.baked);
                worstScalarHour = hours;
                worstScalarWhat = "baked scale";
            }
            // The fog range may move, but not teleport.
            EXPECT_LT(std::fabs(now.fogStart - previous.fogStart), 20.0f) << weather.name << " at " << hours << " h";
            EXPECT_LT(std::fabs(now.fogEnd - previous.fogEnd), 80.0f) << weather.name << " at " << hours << " h";
            previous = now;
        }
        // 0.02 between two applications is about 5/255 per channel: below the point where a
        // transition reads as a step rather than a fade.
        EXPECT_LT(worstColour, 0.02f) << weather.name << ": " << worstWhat << " jumps by "
                                      << worstColour << " at " << worstHour << " h";
        // The two scalars sweep further than a colour does -- the lamps go the whole way from off
        // to on across a dawn -- so they get their own, looser bound.
        EXPECT_LT(worstScalar, 0.05f) << weather.name << ": " << worstScalarWhat << " jumps by "
                                      << worstScalar << " at " << worstScalarHour << " h";
    }
}

// The representative hours the brief asks for, in one place, so the shape of a day is visible in
// the test log rather than only in a screenshot.
TEST(LightingRig, TheShapeOfADayIsSensible)
{
    LightingRig rig;
    for (const float hour : {6.0f, 9.0f, 13.0f, 17.0f, 20.0f, 21.5f, 0.0f}) {
        rig.SetTimeOfDay(hour);
        std::cout << "  " << hour << " h: sun " << rig.SunElevationDeg() << " deg, azimuth "
                  << rig.SunAzimuthDeg() << " deg, lamps " << rig.LampFactor()
                  << ", baked scale " << rig.BakedLightingScale(LightingRig::BakeReference()).X << "\n";
    }
    // Noon is the brightest and midnight the darkest, and the lamps are the other way round.
    const LightingRig reference = LightingRig::BakeReference();
    rig.SetTimeOfDay(13.0f);
    const float noonLight = rig.BakedLightingScale(reference).X;
    const float noonLamps = rig.LampFactor();
    rig.SetTimeOfDay(0.0f);
    const float midnightLight = rig.BakedLightingScale(reference).X;
    const float midnightLamps = rig.LampFactor();
    EXPECT_GT(noonLight, midnightLight * 8.0f);
    EXPECT_LT(noonLamps, 0.05f);
    EXPECT_GT(midnightLamps, 0.9f);
    // Six in the morning and eight in the evening are both daylight with the sun low, and the
    // sun is in the east in the morning and the west in the evening.
    rig.SetTimeOfDay(6.0f);
    EXPECT_GT(rig.SunElevationDeg(), 0.0f);
    EXPECT_LT(rig.SunElevationDeg(), 25.0f);
    EXPECT_LT(rig.SunAzimuthDeg(), 180.0f) << "the morning sun should be in the east";
    rig.SetTimeOfDay(20.0f);
    EXPECT_GT(rig.SunElevationDeg(), 0.0f);
    EXPECT_GT(rig.SunAzimuthDeg(), 180.0f) << "the evening sun should be in the west";
}
