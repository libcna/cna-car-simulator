// Night reflections on a wet road: every light near the road throws a long, soft streak across
// the wet asphalt towards the viewer, as street lamps and headlamps do on a rainy evening.
#pragma once

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace CarSim::Render
{
    struct ReflectedLight
    {
        Microsoft::Xna::Framework::Vector3 position{};   // the lamp itself, world
        Microsoft::Xna::Framework::Vector3 colour{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;                          // 0..1
    };

    /// One streak as geometry: the four corners on the ground, near the light first. Kept
    /// separate from drawing so the shape can be tested.
    struct ReflectionStreak
    {
        Microsoft::Xna::Framework::Vector3 corners[4];
        float alpha = 0.0f;
    };

    class WetReflections
    {
    public:
        static constexpr int kMaxStreaks = 160;
        static constexpr float kRangeM = 140.0f;

        explicit WetReflections(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /// Builds the streak for one light seen from `camera`; false when there is none (too far,
        /// too faint, or the camera stands on the light).
        [[nodiscard]] static bool Streak(const ReflectedLight& light, const Microsoft::Xna::Framework::Vector3& camera, float wetness,
                                         const std::function<float(float, float)>& groundHeight, ReflectionStreak& out);

        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::Vector3& camera,
                  const std::vector<ReflectedLight>& lights, float wetness, const std::function<float(float, float)>& groundHeight);

        [[nodiscard]] int StreaksLastFrame() const { return streaks_; }

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RasterizerState> biased_;
        int streaks_ = 0;
    };
}
