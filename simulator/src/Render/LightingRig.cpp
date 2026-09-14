#include "CarSim/Render/LightingRig.hpp"

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    LightingRig::LightingRig()
    {
        // Azimuth 135 degrees (south-east; +X east, -Z north -> south-east is (+x, +z)),
        // elevation 48 degrees. Direction points from the sun towards the scene.
        const float azimuth = 135.0f * 3.14159265f / 180.0f;
        const float elevation = 48.0f * 3.14159265f / 180.0f;
        const Vector3 toSun(std::sin(azimuth) * std::cos(elevation), std::sin(elevation), std::cos(azimuth) * std::cos(elevation) * -1.0f);
        Vector3 dir = -toSun;
        dir.Normalize();
        sunDirection = dir;
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

    float LightingRig::SunLambert(const Vector3& normal) const
    {
        return std::max(0.0f, Vector3::Dot(normal, -sunDirection));
    }
}
