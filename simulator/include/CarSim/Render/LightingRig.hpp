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
    };
}
