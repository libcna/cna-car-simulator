// Falling rain around the camera: a slab of streak cards that scrolls with the wind and wraps
// around the viewer, plus the mist the road throws up. Project-owned geometry drawn through
// BasicEffect; nothing about it is renderer specific.
#pragma once

#include "CarSim/Core/Weather.hpp"
#include "CarSim/Render/Camera.hpp"
#include "CarSim/Render/GpuMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    class RainRenderer
    {
    public:
        explicit RainRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /// Advances the fall. `dt` is real seconds.
        void Update(float dt, const Core::WeatherState& weather);

        /// Draws the drops around `camera`. Does nothing while it is not raining.
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection, const Microsoft::Xna::Framework::Vector3& cameraPosition,
                  const Microsoft::Xna::Framework::Vector3& fogColor);

        [[nodiscard]] int DrawCallsLastFrame() const { return drawCalls_; }

        /// Number of drops in the slab (the full set is drawn when `rain` is 1).
        static constexpr int kDropCount = 900;
        /// Half-size of the slab the drops live in, in metres.
        static constexpr float kSlabM = 22.0f;

    private:
        struct Drop
        {
            Microsoft::Xna::Framework::Vector3 offset;   // relative to the camera, wrapped into the slab
            float lengthM = 0.6f;
            float alpha = 1.0f;
        };

        std::vector<Drop> drops_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture_;
        Microsoft::Xna::Framework::Vector3 fall_{0.0f, -18.0f, 0.0f};
        float rain_ = 0.0f;
        int drawCalls_ = 0;
    };
}
