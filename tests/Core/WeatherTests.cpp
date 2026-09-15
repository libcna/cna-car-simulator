// The weather presets, their transitions and how they reach the lighting rig and the tyres.
#include "CarSim/Core/Weather.hpp"
#include "CarSim/Render/LightingRig.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
