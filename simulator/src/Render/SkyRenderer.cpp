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
        Refresh(device);

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

        cloudTexture_ = UploadTexture(device, Textures::CloudLayer(512, 77u, rig_.cloudCover), true);
        cloudTextureCover_ = rig_.cloudCover;
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

    void SkyRenderer::Refresh(GraphicsDevice& device)
    {
        // The cloud layer is regenerated only when the cover has moved enough to show, since it
        // is a 512 px procedural texture.
        if (std::fabs(rig_.cloudCover - cloudTextureCover_) > 0.10f) {
            cloudTexture_ = UploadTexture(device, Textures::CloudLayer(512, 77u, rig_.cloudCover), true);
            cloudTextureCover_ = rig_.cloudCover;
        }
        // Dome colours follow the rig: zenith to horizon, warmed towards the sun near sunrise
        // and sunset so the glow sits where the sun actually is.
        MeshData dome;
        const int rings = 24;
        const int segments = 48;
        const Vector3 toSun = -rig_.sunDirection;
        const float sunAzimuth = std::atan2(toSun.X, -toSun.Z);
        const float lowSun = std::clamp(1.0f - std::fabs(rig_.SunElevationDeg()) / 14.0f, 0.0f, 1.0f);
        const Vector3 glow(1.00f, 0.55f, 0.28f);
        for (int r = 0; r <= rings; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(rings);
            const float elevation = (t - 0.35f) / 0.65f * std::numbers::pi_v<float> * 0.5f;
            const float y = std::sin(elevation);
            const float radius = std::cos(elevation);
            Vector3 base;
            if (y >= 0.0f) {
                base = Vector3::Lerp(rig_.horizonColor, rig_.zenithColor, std::pow(y, 0.55f));
            } else {
                base = Vector3::Lerp(rig_.horizonColor, rig_.fogColor * 0.9f, std::min(1.0f, -y * 4.0f));
            }
            for (int s2 = 0; s2 <= segments; ++s2) {
                const float a = static_cast<float>(s2) / static_cast<float>(segments) * 2.0f * std::numbers::pi_v<float>;
                MeshVertex v;
                v.position = Vector3(std::cos(a) * radius, y, std::sin(a) * radius);
                v.normal = -v.position;
                Vector3 color = base;
                if (lowSun > 0.0f) {
                    // Angular distance from the sun's bearing, and height above the horizon.
                    const float bearing = std::atan2(v.position.X, -v.position.Z);
                    float delta = std::fabs(bearing - sunAzimuth);
                    if (delta > std::numbers::pi_v<float>) delta = 2.0f * std::numbers::pi_v<float> - delta;
                    const float near = std::clamp(1.0f - delta / (std::numbers::pi_v<float> * 0.55f), 0.0f, 1.0f);
                    const float low = std::clamp(1.0f - std::max(0.0f, y) / 0.35f, 0.0f, 1.0f);
                    color = Vector3::Lerp(color, glow, lowSun * near * near * low * 0.75f);
                }
                v.color = ToColor(color);
                dome.AddVertex(v);
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s2 = 0; s2 < segments; ++s2) {
                const auto i0 = static_cast<std::uint32_t>(r * (segments + 1) + s2);
                const auto i1 = i0 + 1;
                const auto i2 = i0 + static_cast<std::uint32_t>(segments + 1);
                const auto i3 = i2 + 1;
                dome.AddQuad(i0, i2, i3, i1);   // viewed from inside
            }
        }
        dome_ = GpuMesh::Create(device, dome, VertexLayout::PositionColor);

        if (!stars_) {
            // A fixed field of small quads on the dome; drawn additively and faded in at night.
            MeshData field;
            std::uint32_t seed = 12345u;
            const auto rnd = [&seed]() {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<float>((seed >> 8) & 0xFFFFu) / 65535.0f;
            };
            for (int i = 0; i < 420; ++i) {
                const float azimuth = rnd() * 2.0f * std::numbers::pi_v<float>;
                const float height = 0.06f + rnd() * 0.92f;                 // sin(elevation)
                const float radius = std::sqrt(std::max(0.0f, 1.0f - height * height));
                const Vector3 centre(std::cos(azimuth) * radius, height, std::sin(azimuth) * radius);
                const float size = 0.0016f + rnd() * 0.0032f;
                Vector3 up(0.0f, 1.0f, 0.0f);
                Vector3 right = Vector3::Cross(up, centre);
                if (right.LengthSquared() < 1e-6f) right = Vector3(1.0f, 0.0f, 0.0f);
                right.Normalize();
                Vector3 top = Vector3::Cross(centre, right);
                top.Normalize();
                const float brightness = 0.35f + 0.65f * rnd();
                // Star colour: mostly white, a few warm and a few blue-white, and the blue channel
                // dims with the rest. Leaving blue at 255 while the others dimmed turned every
                // faint star into a saturated blue dot -- a field of blue squares on the sky.
                const float warm = 0.85f + 0.30f * rnd();
                const Color core(static_cast<int>(std::min(255.0f, 250.0f * brightness * warm)),
                                 static_cast<int>(std::min(255.0f, 248.0f * brightness)),
                                 static_cast<int>(std::min(255.0f, 255.0f * brightness / std::max(0.7f, warm))), 255);
                // A fan with a bright centre and a dark rim: drawn additively that is a soft point
                // of light rather than a hard-edged square.
                const std::uint32_t base = static_cast<std::uint32_t>(field.vertices.size());
                {
                    MeshVertex v;
                    v.position = centre;
                    v.normal = -centre;
                    v.color = core;
                    field.AddVertex(v);
                }
                constexpr int kRim = 6;
                for (int k = 0; k < kRim; ++k) {
                    const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(kRim);
                    MeshVertex v;
                    v.position = centre + right * (std::cos(a) * size * 2.2f) + top * (std::sin(a) * size * 2.2f);
                    v.normal = -v.position;
                    v.color = Color(0, 0, 0, 255);
                    field.AddVertex(v);
                }
                for (int k = 0; k < kRim; ++k) {
                    field.indices.push_back(base);
                    field.indices.push_back(base + 1 + static_cast<std::uint32_t>((k + 1) % kRim));
                    field.indices.push_back(base + 1 + static_cast<std::uint32_t>(k));
                }
            }
            stars_ = GpuMesh::Create(device, field, VertexLayout::PositionColor);
        }
    }

    void SkyRenderer::Draw(GraphicsDevice& device, const CameraPose& camera, const float aspect)
    {
        Draw(device, camera.View(), camera.Projection(aspect), camera.position, false);
    }

    void SkyRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const Vector3& position, const bool mirrored)
    {
        CameraPose camera;
        camera.position = position;
        // The mirror has a 320 m far plane. A full-size sky dome sits behind that plane and
        // leaves its daytime-blue clear colour showing through at night.
        const float radius = mirrored ? 260.0f : camera.farPlane * 0.85f;

        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);

        colorEffect_->setWorldProperty(Matrix::CreateScale(radius) * Matrix::CreateTranslation(camera.position));
        colorEffect_->setViewProperty(view);
        colorEffect_->setProjectionProperty(projection);
        ApplyAll(*colorEffect_, device, *dome_);

        // Stars fade in once the sun is below the horizon and are gone by civil twilight.
        // Cloud hides the stars and the moon: a solid lid leaves nothing of either.
        const float clearSky = std::clamp(1.0f - rig_.cloudCover * 1.05f, 0.0f, 1.0f);
        const float starAlpha = std::clamp((-rig_.SunElevationDeg() - 2.0f) / 8.0f, 0.0f, 1.0f) * clearSky;
        if (stars_ && starAlpha > 0.01f) {
            device.setBlendStateProperty(BlendState::Additive);
            colorEffect_->setWorldProperty(Matrix::CreateScale(radius * 0.98f) * Matrix::CreateTranslation(camera.position));
            colorEffect_->setDiffuseColorProperty(Vector3(starAlpha, starAlpha, starAlpha));
            ApplyAll(*colorEffect_, device, *stars_);
            colorEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        }

        // The sun by day, the moon by night: the moon rides the sun's path, half a day behind.
        const bool night = rig_.SunElevationDeg() < -1.0f;
        Vector3 toBody = -rig_.sunDirection;
        if (night) {
            LightingRig moon = rig_;
            moon.SetTimeOfDay(rig_.timeOfDayHours + 12.0f);
            toBody = -moon.sunDirection;
            if (toBody.Y < 0.05f) toBody = Vector3(toBody.X, 0.05f, toBody.Z);
        }
        toBody.Normalize();
        const Vector3 sunPos = camera.position + toBody * (radius * 0.95f);
        const float sunSize = radius * 0.95f * (night ? 0.030f : 0.045f);
        const Matrix billboard = Matrix::CreateBillboard(sunPos, camera.position, Vector3(0.0f, 1.0f, 0.0f), std::nullopt);
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        textureEffect_->setTextureProperty(sunTexture_.get());
        textureEffect_->setWorldProperty(Matrix::CreateScale(sunSize) * billboard);
        textureEffect_->setViewProperty(view);
        textureEffect_->setProjectionProperty(projection);
        textureEffect_->setAlphaProperty(1.0f);
        const Vector3 bodyColour = night ? Vector3(0.82f, 0.85f, 0.92f) * clearSky
                                        : Vector3(1.0f, 1.0f, 1.0f) * std::clamp(1.0f - rig_.cloudCover * 1.15f, 0.0f, 1.0f);
        textureEffect_->setDiffuseColorProperty(bodyColour);
        ApplyAll(*textureEffect_, device, *sun_);
        textureEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        textureEffect_->setTextureProperty(cloudTexture_.get());
        float cloudLight = std::clamp(0.10f + 0.90f * (rig_.SunElevationDeg() + 6.0f) / 14.0f, 0.10f, 1.0f);
        cloudLight *= 1.0f - 0.35f * rig_.rainAmount;   // a raining lid is darker still
        textureEffect_->setDiffuseColorProperty(Vector3(cloudLight, cloudLight, cloudLight * 1.02f));
        const float cloudExtent = radius * 0.9f;
        // A solid lid hangs lower, so it covers more of the sky from horizon to horizon.
        const float cloudBase = 1600.0f - 900.0f * rig_.cloudCover;
        textureEffect_->setWorldProperty(Matrix::CreateScale(cloudExtent, 1.0f, cloudExtent) *
                                         Matrix::CreateTranslation(camera.position.X, camera.position.Y + cloudBase, camera.position.Z));
        ApplyAll(*textureEffect_, device, *clouds_);
        textureEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
    }
}
