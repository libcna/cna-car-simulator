// The weather presets, their transitions and how they reach the lighting rig and the tyres.
#include "CarSim/Core/Weather.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace CarSim;
using Microsoft::Xna::Framework::Vector3;

TEST(Weather, NamesRoundTripAndUnknownNamesAreRejected)
{
    for (int i = 0; i < static_cast<int>(Core::WeatherKind::Count); ++i) {
        const auto kind = static_cast<Core::WeatherKind>(i);
        Core::WeatherKind parsed = Core::WeatherKind::Count;
        ASSERT_TRUE(Core::WeatherFromName(Core::ToString(kind), parsed)) << Core::ToString(kind);
        EXPECT_EQ(parsed, kind);
    }
    Core::WeatherKind kind = Core::WeatherKind::Clear;
    EXPECT_TRUE(Core::WeatherFromName("RAIN", kind));
    EXPECT_EQ(kind, Core::WeatherKind::Rain);
    EXPECT_TRUE(Core::WeatherFromName("few clouds", kind));
    EXPECT_EQ(kind, Core::WeatherKind::FewClouds);
    EXPECT_FALSE(Core::WeatherFromName("hurricane", kind));
}

TEST(Weather, TheSkyChangesGraduallyAndTheRoadDriesSlowly)
{
    Core::WeatherState w;
    w.Snap(Core::WeatherKind::Clear);
    EXPECT_LT(w.cloudCover, 0.1f);
    EXPECT_FLOAT_EQ(w.rain, 0.0f);
    EXPECT_FLOAT_EQ(w.wetness, 0.0f);

    // Asking for rain does not make it rain at once.
    w.Set(Core::WeatherKind::Rain);
    w.Update(1.0f);
    EXPECT_LT(w.rain, 0.1f);
    EXPECT_LT(w.cloudCover, 0.2f);
    for (int i = 0; i < 600; ++i) w.Update(1.0f);   // ten minutes
    EXPECT_GT(w.rain, 0.9f);
    EXPECT_GT(w.cloudCover, 0.9f);
    EXPECT_GT(w.wetness, 0.9f);

    // When it stops the road stays wet for a while.
    w.Set(Core::WeatherKind::Clear);
    for (int i = 0; i < 180; ++i) w.Update(1.0f);   // three minutes
    EXPECT_LT(w.rain, 0.05f);
    EXPECT_GT(w.wetness, 0.5f) << "the road should still be wet just after the rain stops";
    for (int i = 0; i < 2400; ++i) w.Update(1.0f);  // forty more minutes
    EXPECT_LT(w.wetness, 0.1f);
}

TEST(Weather, TheCycleVisitsEveryPreset)
{
    Core::WeatherState w;
    w.Snap(Core::WeatherKind::Clear);
    std::vector<Core::WeatherKind> seen;
    for (int i = 0; i < static_cast<int>(Core::WeatherKind::Count); ++i) {
        seen.push_back(w.kind);
        w.Set(w.Next());
    }
    EXPECT_EQ(w.kind, Core::WeatherKind::Clear);   // back where it started
    for (int i = 0; i < static_cast<int>(Core::WeatherKind::Count); ++i) {
        EXPECT_NE(std::find(seen.begin(), seen.end(), static_cast<Core::WeatherKind>(i)), seen.end());
    }
}

TEST(Weather, CloudFlattensTheLightWithoutBrighteningTheNight)
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    Render::LightingRig clear;
    clear.SetTimeOfDay(13.0f);
    Render::LightingRig overcast;
    overcast.SetTimeOfDay(13.0f);
    overcast.SetWeather(Core::WeatherState::TargetCloudCover(Core::WeatherKind::Overcast), 0.0f);

    // The key light collapses, the dome takes over, and the total drops but not by much.
    EXPECT_LT(overcast.sunColor.Y, 0.25f * clear.sunColor.Y);
    EXPECT_GT(overcast.skyAmbient.Y, clear.skyAmbient.Y);
    EXPECT_LT(overcast.Irradiance(up).Y, clear.Irradiance(up).Y);
    EXPECT_GT(overcast.Irradiance(up).Y, 0.5f * clear.Irradiance(up).Y);
    // A vertical wall facing away from the sun gains, because the dome replaces the sun.
    const Vector3 north(0.0f, 0.0f, -1.0f);
    EXPECT_GT(overcast.Irradiance(north).Y, 0.9f * clear.Irradiance(north).Y);
    // And the view closes in.
    EXPECT_LT(overcast.fogEnd, clear.fogEnd);

    // The same lid at midnight must not turn night into an overcast afternoon.
    Render::LightingRig night;
    night.SetTimeOfDay(1.0f);
    Render::LightingRig nightLid;
    nightLid.SetTimeOfDay(1.0f);
    nightLid.SetWeather(1.0f, 1.0f);
    EXPECT_LT(nightLid.Irradiance(up).Y, night.Irradiance(up).Y);
}

TEST(Weather, RainBringsTheLampsOnEarlier)
{
    Render::LightingRig clear;
    clear.SetTimeOfDay(20.5f);   // sun just above the horizon
    Render::LightingRig wet;
    wet.SetTimeOfDay(20.5f);
    wet.SetWeather(1.0f, 1.0f);
    EXPECT_GT(wet.LampFactor(), clear.LampFactor());
    // Never at noon, though.
    Render::LightingRig noon;
    noon.SetTimeOfDay(13.0f);
    noon.SetWeather(1.0f, 1.0f);
    EXPECT_FLOAT_EQ(noon.LampFactor(), 0.0f);
}

// Every transition between every pair of presets, both ways: the state has to ease, and so does
// the lighting palette computed from it. A front that arrives in a step is the one thing that
// makes weather look scripted rather than weather.
TEST(Weather, EveryTransitionEasesAndNothingPops)
{
    const CarSim::Core::WeatherKind kinds[] = {CarSim::Core::WeatherKind::Clear, CarSim::Core::WeatherKind::FewClouds,
                                               CarSim::Core::WeatherKind::Overcast, CarSim::Core::WeatherKind::Rain};
    const float dt = 1.0f / 60.0f;
    float worstCover = 0.0f;
    float worstRain = 0.0f;
    float worstWetness = 0.0f;
    float worstPalette = 0.0f;
    const char* worstPair = "";
    float slowestSettle = 0.0f;
    float fastestSettle = 1e9f;

    for (const auto from : kinds) {
        for (const auto to : kinds) {
            if (from == to) continue;
            CarSim::Core::WeatherState state;
            state.Snap(from);
            // Let the road dry or soak first, so the transition starts from a settled world.
            for (int i = 0; i < 60 * 60 * 6; ++i) state.Update(dt);
            CarSim::Render::LightingRig rig;
            rig.SetTimeOfDay(13.0f);
            rig.SetWeather(state.cloudCover, state.rain);
            auto previous = state;
            Microsoft::Xna::Framework::Vector3 previousFog = rig.fogColor;
            Microsoft::Xna::Framework::Vector3 previousHorizon = rig.horizonColor;
            Microsoft::Xna::Framework::Vector3 previousSun = rig.sunColor;

            state.Set(to);
            float settleSeconds = 0.0f;
            for (int i = 0; i < 60 * 60 * 20; ++i) {   // up to twenty minutes
                state.Update(dt);
                rig.SetWeather(state.cloudCover, state.rain);
                worstCover = std::max(worstCover, std::fabs(state.cloudCover - previous.cloudCover));
                worstRain = std::max(worstRain, std::fabs(state.rain - previous.rain));
                worstWetness = std::max(worstWetness, std::fabs(state.wetness - previous.wetness));
                const float palette = std::max({(rig.fogColor - previousFog).Length(),
                                                (rig.horizonColor - previousHorizon).Length(),
                                                (rig.sunColor - previousSun).Length()});
                if (palette > worstPalette) {
                    worstPalette = palette;
                    worstPair = CarSim::Core::ToString(to);
                }
                previous = state;
                previousFog = rig.fogColor;
                previousHorizon = rig.horizonColor;
                previousSun = rig.sunColor;
                settleSeconds += dt;
                if (std::fabs(state.cloudCover - CarSim::Core::WeatherState::TargetCloudCover(to)) < 0.01f &&
                    std::fabs(state.rain - CarSim::Core::WeatherState::TargetRain(to)) < 0.01f) {
                    break;
                }
            }
            slowestSettle = std::max(slowestSettle, settleSeconds);
            fastestSettle = std::min(fastestSettle, settleSeconds);
        }
    }

    std::cout << "  worst step per frame: cover " << worstCover << ", rain " << worstRain
              << ", wetness " << worstWetness << ", palette " << worstPalette << " (" << worstPair << ")\n"
              << "  transitions settle between " << fastestSettle << " s and " << slowestSettle << " s\n";
    // Nothing may move by more than a thousandth of its range in one frame at 60 Hz: that is a
    // fade of at least sixteen seconds end to end, which is a front arriving, not a switch.
    EXPECT_LT(worstCover, 0.001f);
    EXPECT_LT(worstRain, 0.001f);
    EXPECT_LT(worstWetness, 0.001f);
    EXPECT_LT(worstPalette, 0.002f) << "the palette pops on a " << worstPair << " transition";
    // And a front takes a while to arrive but does arrive.
    EXPECT_GT(fastestSettle, 20.0f) << "the weather switches rather than changing";
    EXPECT_LT(slowestSettle, 15.0f * 60.0f) << "a front that never settles is not a front";
}
