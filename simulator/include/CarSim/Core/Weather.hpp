// Continuous weather the world renders and drives on: cloud cover, falling rain and how wet the
// road is. Preset kinds set the targets; the state eases towards them so a change of weather is
// a transition rather than a cut. Pure data and arithmetic, shared by Render (lighting, sky,
// rain) and Sim (grip), so it lives in Core and depends on nothing.
#pragma once

#include <string>

namespace CarSim::Core
{
    enum class WeatherKind
    {
        Clear,       // cloudless summer sky
        FewClouds,   // scattered fair-weather cloud (the default)
        Overcast,    // solid grey lid, no direct sun
        Rain,        // overcast and raining
        Fog,         // still air, visibility down to about 150 m
        Snow,        // overcast and snowing; the snow settles and takes grip off the road
        Count
    };

    [[nodiscard]] const char* ToString(WeatherKind kind);
    [[nodiscard]] const char* Describe(WeatherKind kind);
    /// Case-insensitive; accepts "clear", "few", "fewclouds", "cloudy", "overcast", "rain", "fog",
    /// "snow".
    [[nodiscard]] bool WeatherFromName(const std::string& name, WeatherKind& out);

    struct WeatherState
    {
        /// The preset being eased towards.
        WeatherKind kind = WeatherKind::FewClouds;
        float cloudCover = 0.35f;   // 0 = cloudless, 1 = solid lid
        float rain = 0.0f;          // 0 = dry, 1 = heavy rain
        /// How wet the road is. It rises while it rains and dries out over the following minutes,
        /// so the road still shines after a shower has passed.
        float wetness = 0.0f;
        float fog = 0.0f;           // 0 = clear air, 1 = dense fog
        float snow = 0.0f;          // 0 = none, 1 = heavy snowfall
        /// Snow lying on the ground and the roads. It builds up over a few minutes of snowfall
        /// and melts away slowly afterwards (the melt wets the road).
        float snowCover = 0.0f;
        float windFromDeg = 250.0f; // bearing the wind blows from, degrees clockwise from north
        float windSpeedMs = 3.0f;

        /// Sets the preset and eases towards it from the current values.
        void Set(WeatherKind next);
        /// Sets the preset and jumps straight to it (start-up, captures, tests).
        void Snap(WeatherKind next);
        /// Advances the transition and the drying of the road by `dt` seconds.
        void Update(float dt);
        /// The next preset in the cycle, for the key binding.
        [[nodiscard]] WeatherKind Next() const;

        [[nodiscard]] static float TargetCloudCover(WeatherKind kind);
        [[nodiscard]] static float TargetRain(WeatherKind kind);
        [[nodiscard]] static float TargetWindSpeed(WeatherKind kind);
        [[nodiscard]] static float TargetFog(WeatherKind kind);
        [[nodiscard]] static float TargetSnow(WeatherKind kind);
    };
}
