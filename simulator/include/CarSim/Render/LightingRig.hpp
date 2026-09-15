// The fixed daytime lighting environment shared by every effect.
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"

namespace CarSim::Render
{
    /// Summer daylight over central Europe, driven by a time of day: the sun follows a solar
    /// path for the rig's latitude and declination, and the key light, ambient, fog and sky
    /// colours follow its elevation from night through twilight to full day.
    struct LightingRig
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;

        /// Local clock hour the rig was last set to (solar noon is at 13:00, as in CEST).
        float timeOfDayHours = 10.5f;
        float latitudeDeg = 49.8f;          // Bohemia
        float sunDeclinationDeg = 20.0f;    // early summer
        /// Weather the palette is computed under: cloud cover softens and kills the sun, rain
        /// greys the air and shortens the view. Both are applied inside SetTimeOfDay.
        float cloudCover = 0.0f;
        float rainAmount = 0.0f;

        Vector3 sunDirection;            // unit vector pointing FROM the key light towards the scene
        float sunElevationDeg = 0.0f;    // of the sun itself, negative at night (the key light is the moon then)
        float sunAzimuthDeg = 0.0f;      // clockwise from north
        // Exposure: a sunlit horizontal surface receives about ambient + sky + sun * cos(42 deg)
        // = 1.0, so textures keep their contrast instead of clipping to white.
        Vector3 sunColor{0.98f, 0.93f, 0.84f};
        Vector3 skyAmbient{0.21f, 0.23f, 0.28f};
        Vector3 skyFillColor{0.15f, 0.18f, 0.24f};    // soft light from the sky dome (fill from above)
        Vector3 groundBounceColor{0.10f, 0.09f, 0.07f}; // light bounced from the ground (fill from below)
        Vector3 fogColor{0.76f, 0.82f, 0.90f};
        float fogStart = 300.0f;
        float fogEnd = 2600.0f;
        Vector3 zenithColor{0.18f, 0.38f, 0.76f};
        Vector3 horizonColor{0.80f, 0.86f, 0.93f};

        LightingRig();

        /// Sun as light 0, sky fill as light 1, ground bounce as light 2.
        void Apply(Microsoft::Xna::Framework::Graphics::BasicEffect& effect) const;
        void Apply(Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect& effect) const;

        /// Direct lighting term (N.L, no shadow) for baking and dashboards.
        [[nodiscard]] float SunLambert(const Vector3& normal) const;
        /// Diffuse irradiance (RGB) of a surface with this normal under the three rig lights
        /// plus ambient, matching what BasicEffect computes; used to bake road vertex colours.
        [[nodiscard]] Vector3 Irradiance(const Vector3& normal) const;

        /// Points the sun at the given local hour and recomputes every colour from its
        /// elevation. Hours outside 0..24 wrap.
        void SetTimeOfDay(float hours);
        /// Sets the cloud cover and rain (0..1) and recomputes the palette at the current hour.
        void SetWeather(float cover, float rain);
        /// How far the sun may move before the palette has to be re-applied. The whole sky
        /// turns over in the twenty minutes around sunrise and sunset, so near the horizon the
        /// step is much finer than it is in the middle of the day, where an hour of sun barely
        /// changes anything. This is what decides whether a dawn reads as a fade or as a series
        /// of steps.
        [[nodiscard]] static float RefreshStepDeg(float elevationDeg);
        /// Sun elevation above the horizon in degrees (negative at night).
        [[nodiscard]] float SunElevationDeg() const { return sunElevationDeg; }
        /// Sun azimuth in degrees clockwise from north (0 = north, 90 = east).
        [[nodiscard]] float SunAzimuthDeg() const { return sunAzimuthDeg; }
        /// True once the sun is far enough below the horizon for headlights to matter.
        [[nodiscard]] bool IsNight() const { return SunElevationDeg() < -1.0f; }
        /// How much artificial light the world needs: 0 in daylight, 1 once it is properly dark.
        /// Street lamps, lit windows and headlamp pools fade in and out with it.
        [[nodiscard]] float LampFactor() const;

        /// Multiplier that turns lighting baked under `reference` into lighting for this rig:
        /// the ratio of the irradiance of a horizontal surface, tinted like the current key
        /// light. Used for the terrain macro, road vertex colours and tree cards, which are
        /// baked once and scaled per frame instead of being re-baked as the sun moves.
        [[nodiscard]] Vector3 BakedLightingScale(const LightingRig& reference) const;

        /// The rig the baked world lighting was generated with (fixed mid-morning sun, clear sky).
        [[nodiscard]] static LightingRig BakeReference();

    private:
        /// Folds `cloudCover` and `rainAmount` into the palette SetTimeOfDay has just computed.
        void ApplyWeather(float day);
    };
}
