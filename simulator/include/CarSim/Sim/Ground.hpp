// Ground query interface used by the vehicle suspension, plus simple built-in surfaces.
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>

namespace CarSim::Sim
{
    enum class SurfaceType
    {
        Asphalt,
        Concrete,
        Cobbles,
        Gravel,
        Grass,
        Dirt
    };

    /// Friction multiplier applied to the tyre's peak friction for a surface.
    [[nodiscard]] float SurfaceFrictionFactor(SurfaceType surface);
    /// Additional rolling resistance factor for a surface (1 = asphalt).
    [[nodiscard]] float SurfaceRollingFactor(SurfaceType surface);

    struct GroundHit
    {
        Microsoft::Xna::Framework::Vector3 point{};
        Microsoft::Xna::Framework::Vector3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
        SurfaceType surface = SurfaceType::Asphalt;
    };

    class GroundSurface
    {
    public:
        virtual ~GroundSurface() = default;

        /// Casts a ray; returns true and fills `hit` when the surface is hit within maxDistance.
        [[nodiscard]] virtual bool Raycast(const Microsoft::Xna::Framework::Vector3& origin,
                                           const Microsoft::Xna::Framework::Vector3& direction,
                                           float maxDistance, GroundHit& hit) const = 0;
    };

    /// Infinite horizontal plane at a given height.
    class FlatGround final : public GroundSurface
    {
    public:
        explicit FlatGround(float height = 0.0f, SurfaceType surface = SurfaceType::Asphalt)
            : height_(height), surface_(surface) {}

        [[nodiscard]] bool Raycast(const Microsoft::Xna::Framework::Vector3& origin,
                                   const Microsoft::Xna::Framework::Vector3& direction,
                                   float maxDistance, GroundHit& hit) const override;

    private:
        float height_;
        SurfaceType surface_;
    };

    /// Height-field defined by a function y = f(x, z); the normal is derived numerically.
    class FunctionGround final : public GroundSurface
    {
    public:
        using HeightFunction = std::function<float(float, float)>;

        explicit FunctionGround(HeightFunction height, SurfaceType surface = SurfaceType::Asphalt)
            : height_(std::move(height)), surface_(surface) {}

        [[nodiscard]] bool Raycast(const Microsoft::Xna::Framework::Vector3& origin,
                                   const Microsoft::Xna::Framework::Vector3& direction,
                                   float maxDistance, GroundHit& hit) const override;

    private:
        HeightFunction height_;
        SurfaceType surface_;
    };
}
