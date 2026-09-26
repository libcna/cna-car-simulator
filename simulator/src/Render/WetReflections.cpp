#include "CarSim/Render/WetReflections.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/GpuMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
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

    WetReflections::WetReflections(GraphicsDevice& device)
    {
        // Soft across the streak, brightest just in front of the light and fading towards the
        // viewer. Additive, so black is transparent. v = 0 at the light.
        Image streak(32, 128, Color(0, 0, 0, 255));
        streak.Generate([](int, int, const float u, const float v) {
            const float x = u * 2.0f - 1.0f;
            const float across = std::exp(-x * x * 4.5f);
            const float rise = std::clamp(v / 0.08f, 0.0f, 1.0f);
            const float along = rise * std::pow(1.0f - v, 1.6f);
            const int g = static_cast<int>(std::clamp(across * along, 0.0f, 1.0f) * 255.0f);
            return Color(g, g, g, 255);
        });
        texture_ = UploadTexture(device, streak, true);

        constexpr int capacity = kMaxStreaks * 6;
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

        biased_ = std::make_unique<RasterizerState>();
        biased_->setCullModeProperty(CullMode::None);
        biased_->setDepthBiasProperty(-0.00002f);
        biased_->setSlopeScaleDepthBiasProperty(-1.0f);
    }

    bool WetReflections::Streak(const ReflectedLight& light, const Vector3& camera, const float wetness,
                                const std::function<float(float, float)>& groundHeight, ReflectionStreak& out)
    {
        if (wetness <= 0.05f || light.intensity <= 0.01f) return false;
        const float ground = groundHeight ? groundHeight(light.position.X, light.position.Z) : 0.0f;
        const float height = std::max(0.2f, light.position.Y - ground);
        Vector3 toCamera(camera.X - light.position.X, 0.0f, camera.Z - light.position.Z);
        const float distance = toCamera.Length();
        if (distance < 1.0f || distance > kRangeM) return false;
        toCamera = toCamera * (1.0f / distance);
        const Vector3 side(-toCamera.Z, 0.0f, toCamera.X);
        // A high lamp throws a long streak, a headlamp a short one; never past the viewer.
        const float length = std::min(distance * 0.85f, 2.5f + 2.6f * height);
        const float width = 0.35f + 0.12f * height;
        const auto onGround = [&](const Vector3& p) {
            const float h = groundHeight ? groundHeight(p.X, p.Z) : 0.0f;
            return Vector3(p.X, h + 0.04f, p.Z);
        };
        const Vector3 start(light.position.X, 0.0f, light.position.Z);
        const Vector3 end = start + toCamera * length;
        out.corners[0] = onGround(start - side * width);
        out.corners[1] = onGround(start + side * width);
        out.corners[2] = onGround(end + side * width * 1.4f);
        out.corners[3] = onGround(end - side * width * 1.4f);
        const float fade = 1.0f - std::clamp((distance - kRangeM * 0.6f) / (kRangeM * 0.4f), 0.0f, 1.0f);
        out.alpha = std::clamp(wetness * light.intensity * fade, 0.0f, 1.0f);
        return out.alpha > 0.01f;
    }

    void WetReflections::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Vector3& camera,
                              const std::vector<ReflectedLight>& lights, const float wetness,
                              const std::function<float(float, float)>& groundHeight)
    {
        streaks_ = 0;
        if (wetness <= 0.05f || lights.empty()) return;
        std::vector<VertexPositionColorTexture> verts;
        verts.reserve(static_cast<std::size_t>(std::min<int>(kMaxStreaks, static_cast<int>(lights.size()))) * 6);
        for (const auto& light : lights) {
            if (streaks_ >= kMaxStreaks) break;
            ReflectionStreak s;
            if (!Streak(light, camera, wetness, groundHeight, s)) continue;
            const auto channel = [&](const float c) { return static_cast<int>(std::clamp(c * s.alpha * 0.85f, 0.0f, 1.0f) * 255.0f); };
            const Color colour(channel(light.colour.X), channel(light.colour.Y), channel(light.colour.Z), 255);
            verts.emplace_back(s.corners[0], colour, Vector2(0.0f, 0.0f));
            verts.emplace_back(s.corners[1], colour, Vector2(1.0f, 0.0f));
            verts.emplace_back(s.corners[2], colour, Vector2(1.0f, 1.0f));
            verts.emplace_back(s.corners[0], colour, Vector2(0.0f, 0.0f));
            verts.emplace_back(s.corners[2], colour, Vector2(1.0f, 1.0f));
            verts.emplace_back(s.corners[3], colour, Vector2(0.0f, 1.0f));
            ++streaks_;
        }
        if (verts.empty()) return;
        vertices_->SetData(verts.data(), static_cast<int>(verts.size()));
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(*biased_);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        device.SetVertexBuffer(vertices_.get());
        device.setIndicesProperty(indices_.get());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        effect_->setTextureProperty(texture_.get());
        auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
        for (int i = 0; i < passes.getCountProperty(); ++i) {
            passes[i]->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(verts.size()), 0, static_cast<int>(verts.size()) / 3);
            GpuMesh::RecordSubmission(static_cast<int>(verts.size()) / 3);
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }
}
