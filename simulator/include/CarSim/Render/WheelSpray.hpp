// Water thrown up by tyres on a wet road: soft mist puffs behind every wheel that rolls fast
// enough through the water film, for the player's car and the traffic around it.
#pragma once

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    /// A tyre contact patch that may throw spray this frame.
    struct SprayEmitter
    {
        Microsoft::Xna::Framework::Vector3 contact{};    // world, on the road surface
        Microsoft::Xna::Framework::Vector3 velocity{};   // of the car, world m/s
        float share = 1.0f;                              // 0..1: surface wetness factor (0 on grass)
    };

    struct SprayPuff
    {
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 velocity{};
        float age = 0.0f;
        float lifetime = 0.8f;
        float diameter = 0.3f;
        float opacity = 0.3f;
    };

    /// Kept independent of GraphicsDevice so emission can be tested.
    class WheelSpray
    {
    public:
        static constexpr int kMaxPuffs = 480;
        /// Below this road speed a tyre only splashes, it does not throw a plume.
        static constexpr float kMinSpeedMs = 20.0f / 3.6f;

        void Update(float dt, const std::vector<SprayEmitter>& emitters, float wetness);
        [[nodiscard]] const std::vector<SprayPuff>& Puffs() const { return puffs_; }

    private:
        std::vector<SprayPuff> puffs_;
        std::vector<float> credit_;
        unsigned serial_ = 0;
    };

    class WheelSprayRenderer
    {
    public:
        explicit WheelSprayRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        [[nodiscard]] WheelSpray& Spray() { return spray_; }
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                  const Microsoft::Xna::Framework::Vector3& cameraPosition, const Microsoft::Xna::Framework::Vector3& fogColor);

    private:
        WheelSpray spray_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture_;
    };
}
