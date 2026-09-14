// The fixed daytime lighting environment shared by every effect.
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"

namespace CarSim::Render
{
    /// Late-morning summer sun over central Europe: azimuth from the south-east, ~48 degrees
    /// elevation, warm key light, blue-grey sky ambient and a mild haze for depth.
    struct LightingRig
    {
        using Vector3 = Microsoft::Xna::Framework::Vector3;

        Vector3 sunDirection;            // unit vector pointing FROM the sun towards the scene
        Vector3 sunColor{1.00f, 0.95f, 0.86f};
        Vector3 skyAmbient{0.34f, 0.38f, 0.44f};
        Vector3 skyFillColor{0.30f, 0.36f, 0.46f};    // soft light from the sky dome (fill from above)
        Vector3 groundBounceColor{0.20f, 0.18f, 0.14f}; // light bounced from the ground (fill from below)
        Vector3 fogColor{0.70f, 0.78f, 0.88f};
        float fogStart = 350.0f;
        float fogEnd = 2400.0f;
        Vector3 zenithColor{0.24f, 0.44f, 0.80f};
        Vector3 horizonColor{0.74f, 0.82f, 0.90f};

        LightingRig();

        /// Sun as light 0, sky fill as light 1, ground bounce as light 2.
        void Apply(Microsoft::Xna::Framework::Graphics::BasicEffect& effect) const;
        void Apply(Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect& effect) const;

        /// Direct lighting term (N.L, no shadow) for baking and dashboards.
        [[nodiscard]] float SunLambert(const Vector3& normal) const;
    };
}
