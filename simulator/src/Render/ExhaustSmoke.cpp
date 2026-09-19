#include "CarSim/Render/ExhaustSmoke.hpp"

#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        float Variation(const unsigned serial, const unsigned salt)
        {
            unsigned n = serial * 747796405u + salt * 2891336453u;
            n = (n ^ (n >> 16)) * 2246822519u;
            n ^= n >> 13;
            return static_cast<float>(n % 10000u) / 10000.0f;
        }
    }

    void ExhaustSmoke::SetEnabled(const bool enabled)
    {
        enabled_ = enabled;
        if (!enabled_) {
            puffs_.clear();
            emissionCredit_ = 0.0f;
        }
    }

    void ExhaustSmoke::Emit(const Sim::VehicleState& car, const Vector3& wind)
    {
        const unsigned serial = serial_++;
        const Vector3 tailpipe(0.36f, style_.rideHeight + 0.045f, style_.RearZ() + 0.04f);
        const Vector3 rear = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 1.0f), car.worldMatrix);
        ExhaustPuff puff;
        puff.position = Vector3::Transform(tailpipe, car.worldMatrix) +
                        Vector3((Variation(serial, 1u) - 0.5f) * 0.05f, 0.0f,
                                (Variation(serial, 2u) - 0.5f) * 0.05f);
        puff.velocity = rear * (1.0f + 1.2f * car.throttlePedal) +
                        Vector3((Variation(serial, 3u) - 0.5f) * 0.35f,
                                0.35f + Variation(serial, 4u) * 0.4f, 0.0f) +
                        car.velocity * 0.12f + wind * 0.12f;
        puff.lifetime = 1.3f + Variation(serial, 5u) * 0.7f;
        puff.diameter = 0.16f + Variation(serial, 6u) * 0.09f;
        puffs_.push_back(puff);
    }

    void ExhaustSmoke::Update(const float dt, const Sim::VehicleState& car, const Vector3& wind)
    {
        if (!enabled_ || dt <= 0.0f) return;
        const float step = std::min(dt, 0.1f);
        for (ExhaustPuff& puff : puffs_) {
            puff.age += step;
            puff.position += puff.velocity * step;
            puff.velocity.Y += 0.25f * step;
            puff.diameter += 0.36f * step;
        }
        puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const ExhaustPuff& puff) {
                         return puff.age >= puff.lifetime;
                     }), puffs_.end());

        if (car.flightMode || car.engineState != Sim::EngineState::Running) {
            emissionCredit_ = 0.0f;
            return;
        }
        emissionCredit_ += step * (8.0f + 12.0f * std::clamp(car.throttlePedal, 0.0f, 1.0f));
        while (emissionCredit_ >= 1.0f) {
            emissionCredit_ -= 1.0f;
            if (puffs_.size() < kMaxPuffs) Emit(car, wind);
        }
    }

    ExhaustSmokeRenderer::ExhaustSmokeRenderer(GraphicsDevice& device, const Sim::CarStyle& style)
        : smoke_(style)
    {
        Image sprite(64, 64, Color(255, 255, 255, 0));
        sprite.Generate([](int, int, const float u, const float v) {
            const float x = u * 2.0f - 1.0f;
            const float y = v * 2.0f - 1.0f;
            const float edge = std::clamp((1.0f - std::sqrt(x * x + y * y)) * 2.2f, 0.0f, 1.0f);
            return Color(220, 221, 222, static_cast<int>(edge * edge * 180.0f));
        });
        texture_ = UploadTexture(device, sprite, true);

        constexpr int capacity = ExhaustSmoke::kMaxPuffs * 6;
        vertices_ = std::make_unique<VertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                                    capacity, BufferUsage::WriteOnly);
        std::vector<std::uint32_t> identity(capacity);
        std::iota(identity.begin(), identity.end(), 0u);
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits, capacity, BufferUsage::WriteOnly);
        indices_->SetData(identity.data(), capacity);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(true);
        effect_->setFogEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
    }

    void ExhaustSmokeRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection,
                                    const Vector3& cameraPosition)
    {
        const auto& puffs = smoke_.Puffs();
        if (puffs.empty()) return;
        std::vector<std::size_t> order(puffs.size());
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
            return Vector3::DistanceSquared(puffs[a].position, cameraPosition) >
                   Vector3::DistanceSquared(puffs[b].position, cameraPosition);
        });

        std::vector<VertexPositionColorTexture> verts;
        verts.reserve(puffs.size() * 6);
        for (const std::size_t index : order) {
            const ExhaustPuff& puff = puffs[index];
            Vector3 facing = cameraPosition - puff.position;
            if (facing.LengthSquared() < 1e-6f) continue;
            facing.Normalize();
            Vector3 right = Vector3::Cross(Vector3(0.0f, 1.0f, 0.0f), facing);
            if (right.LengthSquared() < 1e-6f) right = Vector3(1.0f, 0.0f, 0.0f);
            right.Normalize();
            const Vector3 up = Vector3::Cross(facing, right);
            const Vector3 side = right * (puff.diameter * 0.5f);
            const Vector3 top = up * (puff.diameter * 0.5f);
            const float life = 1.0f - puff.age / puff.lifetime;
            const Color tint(230, 232, 233, static_cast<int>(std::clamp(life * 205.0f, 0.0f, 255.0f)));
            const Vector3 a = puff.position - side - top;
            const Vector3 b = puff.position + side - top;
            const Vector3 c = puff.position + side + top;
            const Vector3 d = puff.position - side + top;
            verts.emplace_back(a, tint, Vector2(0.0f, 1.0f));
            verts.emplace_back(b, tint, Vector2(1.0f, 1.0f));
            verts.emplace_back(c, tint, Vector2(1.0f, 0.0f));
            verts.emplace_back(a, tint, Vector2(0.0f, 1.0f));
            verts.emplace_back(c, tint, Vector2(1.0f, 0.0f));
            verts.emplace_back(d, tint, Vector2(0.0f, 0.0f));
        }
        if (verts.empty()) return;
        vertices_->SetData(verts.data(), static_cast<int>(verts.size()));

        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        device.SetVertexBuffer(vertices_.get());
        device.setIndicesProperty(indices_.get());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        effect_->setTextureProperty(texture_.get());
        auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
        for (int i = 0; i < passes.getCountProperty(); ++i) {
            passes[i]->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(verts.size()), 0,
                                         static_cast<int>(verts.size()) / 3);
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
