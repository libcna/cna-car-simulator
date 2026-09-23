// Raindrops on the windscreen and the wipers that clear them, seen from the driver's seat.
#pragma once

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <memory>
#include <vector>

namespace CarSim::Render
{
    struct WindscreenDrop
    {
        Microsoft::Xna::Framework::Vector2 position{};   // metres on the glass: x across, y up the slope
        float radiusM = 0.003f;
        float age = 0.0f;
        float opacity = 1.0f;
    };

    /// Drop simulation in the plane of the windscreen, in metres from the bottom-left corner.
    /// Independent of the GraphicsDevice so it can be tested.
    class WindscreenRain
    {
    public:
        static constexpr int kMaxDrops = 700;

        /// `widthM` x `heightM` is the wiped area; the two wiper pivots sit on its bottom edge.
        void SetGlass(float widthM, float heightM);
        /// `rain` 0..1 intensity, `speedMs` forward speed, `wiperPosition` 0 parked .. 1 far end.
        void Update(float dt, float rain, float speedMs, float wiperPosition);

        [[nodiscard]] const std::vector<WindscreenDrop>& Drops() const { return drops_; }
        [[nodiscard]] float Width() const { return width_; }
        [[nodiscard]] float Height() const { return height_; }

        /// Wiper geometry in glass metres, for the renderer and the tests.
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 Pivot(int wiper) const;
        [[nodiscard]] float ArmLength(int wiper) const;
        /// Blade angle from the glass's +X axis (radians) at a wiper position.
        [[nodiscard]] static float BladeAngle(float wiperPosition);

    private:
        [[nodiscard]] bool Swept(const Microsoft::Xna::Framework::Vector2& p, float from, float to) const;

        std::vector<WindscreenDrop> drops_;
        float width_ = 1.3f;
        float height_ = 0.75f;
        float spawnCredit_ = 0.0f;
        float lastWiper_ = 0.0f;
        unsigned serial_ = 0;
    };

    class WindscreenRainRenderer
    {
    public:
        explicit WindscreenRainRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        [[nodiscard]] WindscreenRain& Rain() { return rain_; }
        /// `glass` is the windscreen quad in the body frame (bottom left, bottom right, top right,
        /// top left); `world` the body-to-world matrix; `light` the ambient level for the drops.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::Matrix& world,
                  const std::array<Microsoft::Xna::Framework::Vector3, 4>& glass, float wiperPosition,
                  const Microsoft::Xna::Framework::Vector3& light);

    private:
        WindscreenRain rain_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> dropTexture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
    };
}
