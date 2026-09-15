#include "CarSim/Render/RainRenderer.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        /// Deterministic unit random from an index and a salt, so every run rains the same way.
        float Hash01(const unsigned index, const unsigned salt)
        {
            unsigned h = index * 2654435761u + salt * 2246822519u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            h *= 3266489917u;
            h ^= h >> 16;
            return static_cast<float>(h % 100000u) / 100000.0f;
        }

        float Wrap(const float value, const float half)
        {
            const float span = half * 2.0f;
            float v = std::fmod(value + half, span);
            if (v < 0.0f) v += span;
            return v - half;
        }
    }

    RainRenderer::RainRenderer(GraphicsDevice& device)
    {
        drops_.resize(kDropCount);
        for (int i = 0; i < kDropCount; ++i) {
            const auto u = static_cast<unsigned>(i);
            Drop& d = drops_[static_cast<std::size_t>(i)];
            d.offset = Vector3((Hash01(u, 1u) * 2.0f - 1.0f) * kSlabM,
                               (Hash01(u, 2u) * 2.0f - 1.0f) * kSlabM,
                               (Hash01(u, 3u) * 2.0f - 1.0f) * kSlabM);
            d.lengthM = 0.45f + 0.55f * Hash01(u, 4u);
            d.alpha = 0.35f + 0.45f * Hash01(u, 5u);
        }

        // A soft vertical streak: bright core, transparent edges, fading at both ends.
        Image streak(16, 64, Color(255, 255, 255, 0));
        streak.Generate([](int, int, const float u, const float v) {
            const float across = 1.0f - std::fabs(u * 2.0f - 1.0f);
            const float along = std::min(1.0f, std::min(v, 1.0f - v) * 6.0f);
            const float a = std::clamp(across * across * along, 0.0f, 1.0f);
            return Color(230, 236, 245, static_cast<int>(a * 255.0f));
        });
        texture_ = UploadTexture(device, streak, true);

        const int vertexCount = kDropCount * 6;
        vertices_ = std::make_unique<VertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(), vertexCount,
                                                   BufferUsage::WriteOnly);
        std::vector<std::uint32_t> identity(static_cast<std::size_t>(vertexCount));
        for (std::size_t i = 0; i < identity.size(); ++i) identity[i] = static_cast<std::uint32_t>(i);
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::ThirtyTwoBits, vertexCount, BufferUsage::WriteOnly);
        indices_->SetData(identity.data(), vertexCount);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(true);
        effect_->setFogEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
    }

    void RainRenderer::Update(const float dt, const Core::WeatherState& weather)
    {
        rain_ = weather.rain;
        if (rain_ <= 0.001f || dt <= 0.0f) {
            return;
        }
        // Heavier rain falls faster and leans further into the wind.
        const float speed = 14.0f + 10.0f * rain_;
        const float wind = weather.windSpeedMs;
        const float bearing = weather.windFromDeg * 3.14159265f / 180.0f;
        fall_ = Vector3(-std::sin(bearing) * wind, -speed, std::cos(bearing) * wind);
        for (Drop& d : drops_) {
            d.offset += fall_ * dt;
            d.offset.X = Wrap(d.offset.X, kSlabM);
            d.offset.Y = Wrap(d.offset.Y, kSlabM);
            d.offset.Z = Wrap(d.offset.Z, kSlabM);
        }
    }

    void RainRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Vector3& cameraPosition,
                            const Vector3& fogColor)
    {
        drawCalls_ = 0;
        if (rain_ <= 0.02f || !vertices_ || !indices_) {
            return;
        }
        // Only as many drops as the rain deserves, always the same ones, so light rain is a
        // subset of heavy rain rather than a different pattern.
        const int count = std::clamp(static_cast<int>(static_cast<float>(kDropCount) * rain_), 1, kDropCount);

        // Streaks lean along the fall direction and face the camera: the card's long axis is the
        // fall, its width is across the line of sight.
        Vector3 fall = fall_;
        if (fall.LengthSquared() < 1e-4f) fall = Vector3(0.0f, -1.0f, 0.0f);
        fall.Normalize();
        std::vector<VertexPositionColorTexture> verts;
        verts.reserve(static_cast<std::size_t>(count) * 6);
        const Vector3 tint(0.72f + 0.28f * fogColor.X, 0.74f + 0.26f * fogColor.Y, 0.78f + 0.22f * fogColor.Z);
        for (int i = 0; i < count; ++i) {
            const Drop& d = drops_[static_cast<std::size_t>(i)];
            const Vector3 centre = cameraPosition + d.offset;
            Vector3 toCamera = cameraPosition - centre;
            if (toCamera.LengthSquared() < 1e-4f) continue;
            toCamera.Normalize();
            Vector3 across = Vector3::Cross(fall, toCamera);
            if (across.LengthSquared() < 1e-6f) continue;
            across.Normalize();
            const Vector3 half = fall * (d.lengthM * (0.7f + 0.6f * rain_) * 0.5f);
            const Vector3 side = across * 0.018f;
            const float a = d.alpha * std::clamp(rain_ * 1.4f, 0.0f, 1.0f);
            const Color colour(static_cast<int>(tint.X * 255.0f), static_cast<int>(tint.Y * 255.0f), static_cast<int>(tint.Z * 255.0f),
                               static_cast<int>(a * 255.0f));
            const Vector3 a0 = centre - half - side;
            const Vector3 b0 = centre - half + side;
            const Vector3 c0 = centre + half + side;
            const Vector3 d0 = centre + half - side;
            verts.emplace_back(a0, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(b0, colour, Vector2(1.0f, 1.0f));
            verts.emplace_back(c0, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(a0, colour, Vector2(0.0f, 1.0f));
            verts.emplace_back(c0, colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(d0, colour, Vector2(0.0f, 0.0f));
        }
        if (verts.size() < 3) {
            return;
        }
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
        ++drawCalls_;
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
