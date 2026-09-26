#include "CarSim/Render/WheelSpray.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/GpuMesh.hpp"

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

#include "CarSim/Core/Noise.hpp"

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

    void WheelSpray::Update(const float dt, const std::vector<SprayEmitter>& emitters, const float wetness)
    {
        if (dt <= 0.0f) return;
        const float step = std::min(dt, 0.1f);
        for (SprayPuff& p : puffs_) {
            p.age += step;
            p.position += p.velocity * step;
            // Droplets fall out of the mist and the air slows what is left.
            p.velocity.Y -= 3.5f * step;
            p.velocity *= std::max(0.0f, 1.0f - 1.8f * step);
            p.diameter += 1.1f * step;
        }
        puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const SprayPuff& p) { return p.age >= p.lifetime; }),
                     puffs_.end());

        credit_.resize(emitters.size(), 0.0f);
        const float wet = std::clamp((wetness - 0.15f) / 0.85f, 0.0f, 1.0f);
        if (wet <= 0.0f) {
            std::fill(credit_.begin(), credit_.end(), 0.0f);
            return;
        }
        for (std::size_t i = 0; i < emitters.size(); ++i) {
            const SprayEmitter& e = emitters[i];
            Vector3 horizontal(e.velocity.X, 0.0f, e.velocity.Z);
            const float speed = horizontal.Length();
            if (speed < kMinSpeedMs || e.share <= 0.0f) {
                credit_[i] = 0.0f;
                continue;
            }
            horizontal.Normalize();
            // The plume grows with speed: a few puffs a second at town speed, a curtain on the
            // motorway.
            const float intensity = wet * e.share * std::clamp((speed - kMinSpeedMs) / 22.0f, 0.0f, 1.0f);
            credit_[i] += step * (4.0f + 26.0f * intensity);
            while (credit_[i] >= 1.0f) {
                credit_[i] -= 1.0f;
                if (static_cast<int>(puffs_.size()) >= kMaxPuffs) break;
                const unsigned serial = serial_++;
                SprayPuff p;
                p.position = e.contact + Vector3(0.0f, 0.12f, 0.0f) - horizontal * 0.35f +
                             Vector3((Variation(serial, 1u) - 0.5f) * 0.25f, 0.0f, (Variation(serial, 2u) - 0.5f) * 0.25f);
                // Thrown up and back off the tread; the mist is dragged along behind the car at a
                // fraction of its speed.
                p.velocity = e.velocity * (0.45f + 0.2f * Variation(serial, 3u)) +
                             Vector3((Variation(serial, 4u) - 0.5f) * 1.6f, 1.2f + 1.8f * Variation(serial, 5u),
                                     (Variation(serial, 6u) - 0.5f) * 1.6f);
                p.lifetime = 0.6f + 0.6f * Variation(serial, 7u);
                p.diameter = 0.35f + 0.3f * Variation(serial, 8u);
                p.opacity = (0.12f + 0.22f * intensity) * (0.7f + 0.3f * Variation(serial, 9u));
                puffs_.push_back(p);
            }
        }
    }

    WheelSprayRenderer::WheelSprayRenderer(GraphicsDevice& device)
    {
        // A soft, slightly mottled disc: mist rather than a smoke ball.
        Image sprite(64, 64, Color(255, 255, 255, 0));
        sprite.Generate([](int, int, const float u, const float v) {
            const float x = u * 2.0f - 1.0f;
            const float y = v * 2.0f - 1.0f;
            const float edge = std::clamp(1.0f - std::sqrt(x * x + y * y), 0.0f, 1.0f);
            const float mottle = 0.7f + 0.3f * Noise::Value(u * 8.0f, v * 8.0f, 8, 13u);
            return Color(255, 255, 255, static_cast<int>(std::clamp(edge * edge * mottle, 0.0f, 1.0f) * 255.0f));
        });
        texture_ = UploadTexture(device, sprite, true);

        constexpr int capacity = WheelSpray::kMaxPuffs * 6;
        vertices_ = std::make_unique<VertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(), capacity,
                                                   BufferUsage::WriteOnly);
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

    void WheelSprayRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Vector3& cameraPosition,
                                  const Vector3& fogColor)
    {
        const auto& puffs = spray_.Puffs();
        if (puffs.empty()) return;
        std::vector<std::size_t> order(puffs.size());
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](const std::size_t a, const std::size_t b) {
            return Vector3::DistanceSquared(puffs[a].position, cameraPosition) > Vector3::DistanceSquared(puffs[b].position, cameraPosition);
        });
        // Spray takes the colour of the grey daylight it scatters.
        const Vector3 tint(0.55f + 0.4f * fogColor.X, 0.55f + 0.4f * fogColor.Y, 0.57f + 0.4f * fogColor.Z);
        std::vector<VertexPositionColorTexture> verts;
        verts.reserve(puffs.size() * 6);
        for (const std::size_t index : order) {
            const SprayPuff& p = puffs[index];
            Vector3 facing = cameraPosition - p.position;
            if (facing.LengthSquared() < 1e-6f) continue;
            facing.Normalize();
            Vector3 right = Vector3::Cross(Vector3(0.0f, 1.0f, 0.0f), facing);
            if (right.LengthSquared() < 1e-6f) right = Vector3(1.0f, 0.0f, 0.0f);
            right.Normalize();
            const Vector3 up = Vector3::Cross(facing, right);
            const Vector3 side = right * (p.diameter * 0.5f);
            const Vector3 top = up * (p.diameter * 0.5f);
            const float life = 1.0f - p.age / p.lifetime;
            const float fadeIn = std::clamp(p.age / 0.08f, 0.0f, 1.0f);
            const Color colour(static_cast<int>(std::clamp(tint.X, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(tint.Y, 0.0f, 1.0f) * 255.0f),
                               static_cast<int>(std::clamp(tint.Z, 0.0f, 1.0f) * 255.0f),
                               static_cast<int>(std::clamp(p.opacity * life * fadeIn, 0.0f, 1.0f) * 255.0f));
            const Vector3 a = p.position - side - top;
            const Vector3 b = p.position + side - top;
            const Vector3 c = p.position + side + top;
            const Vector3 d = p.position - side + top;
            verts.emplace_back(a, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(b, colour, Vector2(1.0f, 1.0f));
            verts.emplace_back(c, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(a, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(c, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(d, colour, Vector2(0.0f, 0.0f));
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
            GpuMesh::RecordSubmission(static_cast<int>(verts.size()) / 3);
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
