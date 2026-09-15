#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kDeg = kPi / 180.0f;

        float Clamp01(const float v) { return std::clamp(v, 0.0f, 1.0f); }

        float SmoothStep(const float a, const float b, const float x)
        {
            const float t = Clamp01((x - a) / std::max(1e-5f, b - a));
            return t * t * (3.0f - 2.0f * t);
        }

        Vector3 Mix(const Vector3& a, const Vector3& b, const float t)
        {
            return Vector3::Lerp(a, b, Clamp01(t));
        }
    }

    LightingRig::LightingRig()
    {
        SetTimeOfDay(timeOfDayHours);
    }

    LightingRig LightingRig::BakeReference()
    {
        LightingRig rig;
        rig.SetTimeOfDay(10.5f);   // mid-morning sun: what the terrain macro and roads are baked under
        return rig;
    }

    void LightingRig::SetTimeOfDay(const float hours)
    {
        timeOfDayHours = hours - 24.0f * std::floor(hours / 24.0f);
        // Solar position for the rig's latitude: hour angle measured from solar noon at 13:00,
        // which is where the sun culminates on Czech summer time.
        const float hourAngle = (timeOfDayHours - 13.0f) * 15.0f * kDeg;
        const float decl = sunDeclinationDeg * kDeg;
        const float lat = latitudeDeg * kDeg;
        const float sinElevation = std::clamp(std::sin(decl) * std::sin(lat) + std::cos(decl) * std::cos(lat) * std::cos(hourAngle), -1.0f, 1.0f);
        const float elevation = std::asin(sinElevation);
        const float cosElevation = std::max(1e-4f, std::cos(elevation));
        float cosAzimuth = (std::sin(decl) - sinElevation * std::sin(lat)) / (cosElevation * std::cos(lat));
        cosAzimuth = std::clamp(cosAzimuth, -1.0f, 1.0f);
        float azimuth = std::acos(cosAzimuth);          // from north, 0..pi
        if (hourAngle > 0.0f) azimuth = 2.0f * kPi - azimuth;   // afternoon: west of south
        // +X east, -Z north, azimuth clockwise from north.
        const Vector3 toSun(std::sin(azimuth) * cosElevation, sinElevation, -std::cos(azimuth) * cosElevation);
        Vector3 dir = -toSun;
        dir.Normalize();
        sunDirection = dir;

        const float elevationDeg = elevation / kDeg;
        sunElevationDeg = elevationDeg;
        sunAzimuthDeg = azimuth / kDeg;
        // Day factor: full daylight above 8 degrees, gone once the sun is 2 degrees down.
        const float day = SmoothStep(-2.0f, 8.0f, elevationDeg);
        // Civil twilight glow: strongest just around sunset and sunrise.
        const float dusk = SmoothStep(-9.0f, 1.0f, elevationDeg) * (1.0f - SmoothStep(1.0f, 12.0f, elevationDeg));
        const float night = 1.0f - SmoothStep(-9.0f, -2.0f, elevationDeg);

        // Key light: full strength above 40 degrees, warm and weak near the horizon, off at night.
        const float strength = Clamp01(std::max(0.0f, sinElevation) / std::sin(40.0f * kDeg));
        const Vector3 noonSun(0.98f, 0.93f, 0.84f);
        const Vector3 lowSun(1.00f, 0.60f, 0.32f);
        const float warm = 1.0f - SmoothStep(2.0f, 16.0f, elevationDeg);
        sunColor = Mix(noonSun, lowSun, warm * 0.9f) * strength;
        if (elevationDeg < -1.0f) {
            // Moonlight: a dim, cold key from roughly the opposite side of the sky.
            sunColor = Vector3(0.068f, 0.078f, 0.115f) * (0.35f + 0.65f * night);
            sunDirection = Vector3(-dir.X, -std::fabs(dir.Y) * 0.8f - 0.3f, -dir.Z);
            sunDirection.Normalize();
        }

        const Vector3 dayAmbient(0.21f, 0.23f, 0.28f);
        const Vector3 duskAmbient(0.17f, 0.14f, 0.15f);
        const Vector3 nightAmbient(0.050f, 0.056f, 0.080f);
        skyAmbient = Mix(Mix(nightAmbient, duskAmbient, 1.0f - night), dayAmbient, day);
        skyFillColor = Mix(Vector3(0.027f, 0.032f, 0.052f), Vector3(0.15f, 0.18f, 0.24f), day) + Vector3(0.05f, 0.03f, 0.02f) * dusk;
        groundBounceColor = Mix(Vector3(0.010f, 0.010f, 0.014f), Vector3(0.10f, 0.09f, 0.07f), day);

        const Vector3 dayFog(0.76f, 0.82f, 0.90f);
        const Vector3 duskFog(0.72f, 0.55f, 0.46f);
        const Vector3 nightFog(0.045f, 0.055f, 0.095f);
        fogColor = Mix(Mix(nightFog, duskFog, 1.0f - night), dayFog, day);
        const Vector3 dayZenith(0.18f, 0.38f, 0.76f);
        const Vector3 duskZenith(0.14f, 0.22f, 0.46f);
        const Vector3 nightZenith(0.016f, 0.024f, 0.060f);
        zenithColor = Mix(Mix(nightZenith, duskZenith, 1.0f - night), dayZenith, day);
        const Vector3 dayHorizon(0.80f, 0.86f, 0.93f);
        const Vector3 duskHorizon(0.95f, 0.58f, 0.32f);
        const Vector3 nightHorizon(0.040f, 0.050f, 0.090f);
        horizonColor = Mix(Mix(nightHorizon, duskHorizon, 1.0f - night), dayHorizon, day);
        horizonColor = Mix(horizonColor, duskHorizon, dusk * 0.7f);
        // Night air is clearer but the view fades sooner in the dark.
        fogStart = Mix(Vector3(120.0f, 0.0f, 0.0f), Vector3(300.0f, 0.0f, 0.0f), day).X;
        fogEnd = Mix(Vector3(1400.0f, 0.0f, 0.0f), Vector3(2600.0f, 0.0f, 0.0f), day).X;
    }

    float LightingRig::LampFactor() const
    {
        // Lamps come on as the sun sets (they are already on below the horizon) and go off again
        // once it is properly up.
        return 1.0f - SmoothStep(-4.0f, 6.0f, sunElevationDeg);
    }

    Vector3 LightingRig::BakedLightingScale(const LightingRig& reference) const
    {
        const Vector3 up(0.0f, 1.0f, 0.0f);
        const Vector3 now = Irradiance(up);
        const Vector3 was = reference.Irradiance(up);
        return Vector3(now.X / std::max(1e-3f, was.X), now.Y / std::max(1e-3f, was.Y), now.Z / std::max(1e-3f, was.Z));
    }

    void LightingRig::Apply(BasicEffect& effect) const
    {
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(true);
        effect.setAmbientLightColorProperty(skyAmbient);

        auto& sun = effect.getDirectionalLight0Property();
        sun.setEnabledProperty(true);
        sun.setDirectionProperty(sunDirection);
        sun.setDiffuseColorProperty(sunColor);
        sun.setSpecularColorProperty(sunColor);

        auto& sky = effect.getDirectionalLight1Property();
        sky.setEnabledProperty(true);
        sky.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        sky.setDiffuseColorProperty(skyFillColor);
        sky.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));

        auto& ground = effect.getDirectionalLight2Property();
        ground.setEnabledProperty(true);
        ground.setDirectionProperty(Vector3(0.3f, 1.0f, 0.2f));
        ground.setDiffuseColorProperty(groundBounceColor);
        ground.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));

        effect.setFogEnabledProperty(true);
        effect.setFogColorProperty(fogColor);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);
    }

    void LightingRig::Apply(EnvironmentMapEffect& effect) const
    {
        effect.setAmbientLightColorProperty(skyAmbient);
        auto& sun = effect.getDirectionalLight0Property();
        sun.setEnabledProperty(true);
        sun.setDirectionProperty(sunDirection);
        sun.setDiffuseColorProperty(sunColor);
        sun.setSpecularColorProperty(sunColor);
        auto& sky = effect.getDirectionalLight1Property();
        sky.setEnabledProperty(true);
        sky.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        sky.setDiffuseColorProperty(skyFillColor);
        auto& ground = effect.getDirectionalLight2Property();
        ground.setEnabledProperty(true);
        ground.setDirectionProperty(Vector3(0.3f, 1.0f, 0.2f));
        ground.setDiffuseColorProperty(groundBounceColor);
        effect.setFogEnabledProperty(true);
        effect.setFogColorProperty(fogColor);
        effect.setFogStartProperty(fogStart);
        effect.setFogEndProperty(fogEnd);
    }

    Vector3 LightingRig::Irradiance(const Vector3& normal) const
    {
        Vector3 n = normal;
        if (n.LengthSquared() < 1e-8f) n = Vector3(0.0f, 1.0f, 0.0f);
        n.Normalize();
        Vector3 groundFrom(-0.3f, -1.0f, -0.2f);   // ground bounce travels along (0.3, 1, 0.2)
        groundFrom.Normalize();
        const float sun = std::max(0.0f, Vector3::Dot(n, -sunDirection));
        const float sky = std::max(0.0f, n.Y);
        const float ground = std::max(0.0f, Vector3::Dot(n, groundFrom));
        return skyAmbient + sunColor * sun + skyFillColor * sky + groundBounceColor * ground;
    }

    float LightingRig::SunLambert(const Vector3& normal) const
    {
        return std::max(0.0f, Vector3::Dot(normal, -sunDirection));
    }
}
