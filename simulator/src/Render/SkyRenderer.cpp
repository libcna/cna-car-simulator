#include "CarSim/Render/SkyRenderer.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        Color ToColor(const Vector3& c)
        {
            const auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            return Color(b(c.X), b(c.Y), b(c.Z), 255);
        }

        void ApplyAll(BasicEffect& effect, GraphicsDevice& device, const GpuMesh& mesh)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
            }
        }
    }

    SkyRenderer::SkyRenderer(GraphicsDevice& device, const LightingRig& rig)
        : rig_(rig)
    {
        MeshData dome;
        const int rings = 24;
        const int segments = 48;
        for (int r = 0; r <= rings; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(rings);
            const float elevation = (t - 0.35f) / 0.65f * std::numbers::pi_v<float> * 0.5f;
            const float y = std::sin(elevation);
            const float radius = std::cos(elevation);
            Vector3 color;
            if (y >= 0.0f) {
                color = Vector3::Lerp(rig.horizonColor, rig.zenithColor, std::pow(y, 0.55f));
            } else {
                color = Vector3::Lerp(rig.horizonColor, rig.fogColor * 0.9f, std::min(1.0f, -y * 4.0f));
            }
            for (int s = 0; s <= segments; ++s) {
                const float a = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * std::numbers::pi_v<float>;
                MeshVertex v;
                v.position = Vector3(std::cos(a) * radius, y, std::sin(a) * radius);
                v.normal = -v.position;
                v.color = ToColor(color);
                dome.AddVertex(v);
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < segments; ++s) {
                const auto i0 = static_cast<std::uint32_t>(r * (segments + 1) + s);
                const auto i1 = i0 + 1;
                const auto i2 = i0 + static_cast<std::uint32_t>(segments + 1);
                const auto i3 = i2 + 1;
                dome.AddQuad(i0, i2, i3, i1);   // viewed from inside
            }
        }
        dome_ = GpuMesh::Create(device, dome, VertexLayout::PositionColor);

        Image sunImage(64, 64, Color(255, 255, 255, 0));
        sunImage.Generate([&](int, int, float u, float v) {
            const float d = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
            const float core = std::clamp(1.0f - (d - 0.55f) / 0.12f, 0.0f, 1.0f);
            const float glow = std::clamp(1.0f - d, 0.0f, 1.0f);
            const float a = std::clamp(core + glow * glow * 0.35f, 0.0f, 1.0f);
            return Color(255, 250, 230, static_cast<int>(a * 255.0f));
        });
        sunTexture_ = UploadTexture(device, sunImage, false);
        MeshData sunQuad;
        sunQuad.AddQuad(Vector3(-1, -1, 0), Vector3(1, -1, 0), Vector3(1, 1, 0), Vector3(-1, 1, 0), Vector3(0, 0, 1),
                        Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
        sun_ = GpuMesh::Create(device, sunQuad, VertexLayout::PositionTexture);

        cloudTexture_ = UploadTexture(device, Textures::CloudLayer(512, 77u), true);
        MeshData cloudQuad;
        cloudQuad.AddQuad(Vector3(-1, 0, 1), Vector3(1, 0, 1), Vector3(1, 0, -1), Vector3(-1, 0, -1),
                          Vector3(0, -1, 0), Vector2(0, 6), Vector2(6, 6), Vector2(6, 0), Vector2(0, 0));
        cloudQuad.FlipWinding();   // seen from below
        clouds_ = GpuMesh::Create(device, cloudQuad, VertexLayout::PositionTexture);

        colorEffect_ = std::make_unique<BasicEffect>(device);
        colorEffect_->setLightingEnabledProperty(false);
        colorEffect_->setVertexColorEnabledProperty(true);
        colorEffect_->setTextureEnabledProperty(false);
        colorEffect_->setFogEnabledProperty(false);

        textureEffect_ = std::make_unique<BasicEffect>(device);
        textureEffect_->setLightingEnabledProperty(false);
        textureEffect_->setVertexColorEnabledProperty(false);
        textureEffect_->setTextureEnabledProperty(true);
        textureEffect_->setFogEnabledProperty(false);
    }

    void SkyRenderer::Draw(GraphicsDevice& device, const CameraPose& camera, const float aspect)
    {
        Draw(device, camera.View(), camera.Projection(aspect), camera.position, false);
    }

    void SkyRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Vector3& position, const bool mirrored)
    {
        CameraPose camera;
        camera.position = position;
        const float radius = camera.farPlane * 0.85f;

        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);

        colorEffect_->setWorldProperty(Matrix::CreateScale(radius) * Matrix::CreateTranslation(camera.position));
        colorEffect_->setViewProperty(view);
        colorEffect_->setProjectionProperty(projection);
        ApplyAll(*colorEffect_, device, *dome_);

        const Vector3 toSun = -rig_.sunDirection;
        const Vector3 sunPos = camera.position + toSun * (radius * 0.95f);
        const float sunSize = radius * 0.95f * 0.045f;
        const Matrix billboard = Matrix::CreateBillboard(sunPos, camera.position, Vector3(0.0f, 1.0f, 0.0f), std::nullopt);
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        textureEffect_->setTextureProperty(sunTexture_.get());
        textureEffect_->setWorldProperty(Matrix::CreateScale(sunSize) * billboard);
        textureEffect_->setViewProperty(view);
        textureEffect_->setProjectionProperty(projection);
        textureEffect_->setAlphaProperty(1.0f);
        ApplyAll(*textureEffect_, device, *sun_);

        device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        textureEffect_->setTextureProperty(cloudTexture_.get());
        const float cloudExtent = radius * 0.9f;
        textureEffect_->setWorldProperty(Matrix::CreateScale(cloudExtent, 1.0f, cloudExtent) *
                                         Matrix::CreateTranslation(camera.position.X, camera.position.Y + 1600.0f, camera.position.Z));
        ApplyAll(*textureEffect_, device, *clouds_);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
    }
}
