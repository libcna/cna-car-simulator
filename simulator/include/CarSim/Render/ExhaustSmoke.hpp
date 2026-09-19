// Soft world-space exhaust puffs emitted from the player car's tailpipe.
#pragma once

#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <memory>
#include <vector>

namespace CarSim::Render
{
    struct ExhaustPuff
    {
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 velocity{};
        float age = 0.0f;
        float lifetime = 1.5f;
        float diameter = 0.18f;
    };

    /// Kept independent of GraphicsDevice so emission and the on/off control can be tested.
    class ExhaustSmoke
    {
    public:
        static constexpr int kMaxPuffs = 96;

        explicit ExhaustSmoke(const Sim::CarStyle& style) : style_(style) {}
        void SetEnabled(bool enabled);
        [[nodiscard]] bool Enabled() const { return enabled_; }
        void Update(float dt, const Sim::VehicleState& car, const Microsoft::Xna::Framework::Vector3& wind = {});
        [[nodiscard]] const std::vector<ExhaustPuff>& Puffs() const { return puffs_; }

    private:
        void Emit(const Sim::VehicleState& car, const Microsoft::Xna::Framework::Vector3& wind);

        Sim::CarStyle style_;
        std::vector<ExhaustPuff> puffs_;
        bool enabled_ = true;
        float emissionCredit_ = 0.0f;
        unsigned serial_ = 0;
    };

    class ExhaustSmokeRenderer
    {
    public:
        ExhaustSmokeRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Sim::CarStyle& style);
        [[nodiscard]] ExhaustSmoke& Smoke() { return smoke_; }
        void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                  const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                  const Microsoft::Xna::Framework::Vector3& cameraPosition);

    private:
        ExhaustSmoke smoke_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BasicEffect> effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture_;
    };
}
