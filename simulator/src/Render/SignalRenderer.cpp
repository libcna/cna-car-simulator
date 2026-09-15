#include "CarSim/Render/SignalRenderer.hpp"

#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/MeshData.hpp"

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

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kLensY[3] = {3.28f, 2.98f, 2.68f};   // red on top, then amber, then green
        constexpr float kLensRadius = 0.13f;
        constexpr float kLensZ = 0.115f;   // just proud of the housing face
        constexpr float kSignalRange = 260.0f;

        void ApplyAll(BasicEffect& effect, GraphicsDevice& device, const GpuMesh& mesh)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
            }
        }

        /// A lens quad facing local +z, placed by the head's world matrix.
        void AddLensQuad(MeshData& mesh, const Matrix& world, const float y, const float radius, const float z)
        {
            const Vector3 a = Vector3::Transform(Vector3(-radius, y - radius, z), world);
            const Vector3 b = Vector3::Transform(Vector3(radius, y - radius, z), world);
            const Vector3 c = Vector3::Transform(Vector3(radius, y + radius, z), world);
            const Vector3 d = Vector3::Transform(Vector3(-radius, y + radius, z), world);
            Vector3 n = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 1.0f), world);
            n.Normalize();
            mesh.AddQuad(a, b, c, d, n, Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
        }
    }

    bool SignalRenderer::LensLit(const Traffic::SignalAspect aspect, const int lens)
    {
        switch (aspect) {
            case Traffic::SignalAspect::Red: return lens == 0;
            case Traffic::SignalAspect::RedAmber: return lens == 0 || lens == 1;
            case Traffic::SignalAspect::Amber: return lens == 1;
            case Traffic::SignalAspect::Green: return lens == 2;
        }
        return false;
    }

    SignalRenderer::SignalRenderer(GraphicsDevice& device, const Map::MapWorld& world)
    {
        // A round lens cut out of the quad: opaque in the middle, transparent at the corners.
        Image lens(64, 64, Color(255, 255, 255, 0));
        lens.Generate([](int, int, const float u, const float v) {
            const float dx = u * 2.0f - 1.0f;
            const float dy = v * 2.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp((1.0f - r) * 7.0f, 0.0f, 1.0f);
            // A hint of a fresnel ring so the dark lens still reads as glass.
            const float shade = 0.72f + 0.28f * std::clamp(1.0f - r * 1.4f, 0.0f, 1.0f);
            return Color(static_cast<int>(shade * 255.0f), static_cast<int>(shade * 255.0f), static_cast<int>(shade * 255.0f),
                         static_cast<int>(a * 255.0f));
        });
        lensTexture_ = UploadTexture(device, lens, true);

        Image glow(64, 64, Color(0, 0, 0, 255));
        glow.Generate([](int, int, const float u, const float v) {
            const float dx = u * 2.0f - 1.0f;
            const float dy = v * 2.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(1.0f - r, 0.0f, 1.0f);
            const float f = a * a * (0.35f + 0.65f * a);
            return Color(static_cast<int>(f * 255.0f), static_cast<int>(f * 255.0f), static_cast<int>(f * 255.0f), 255);
        });
        glowTexture_ = UploadTexture(device, glow, true);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(false);
        effect_->setFogEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());

        MeshData dark;
        for (const auto& placed : world.Objects().Signals()) {
            const Matrix headWorld = Matrix::CreateRotationY(placed.headingRad) * Matrix::CreateTranslation(placed.position);
            Head head;
            head.intersection = placed.intersection;
            head.group = placed.group;
            head.centre = Vector3::Transform(Vector3(0.0f, kLensY[1], kLensZ), headWorld);
            for (int i = 0; i < 3; ++i) {
                AddLensQuad(dark, headWorld, kLensY[i], kLensRadius, kLensZ);
                MeshData lit;
                AddLensQuad(lit, headWorld, kLensY[i], kLensRadius, kLensZ + 0.004f);
                head.lens[i] = GpuMesh::Create(device, lit, VertexLayout::PositionNormalTexture);
                MeshData halo;
                AddLensQuad(halo, headWorld, kLensY[i], kLensRadius * 3.2f, kLensZ + 0.008f);
                head.glow[i] = GpuMesh::Create(device, halo, VertexLayout::PositionNormalTexture);
            }
            heads_.push_back(std::move(head));
        }
        if (dark.TriangleCount() > 0) {
            darkLenses_ = GpuMesh::Create(device, dark, VertexLayout::PositionNormalTexture);
        }
    }

    void SignalRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum,
                              const Vector3& cameraPosition, const LightingRig& rig,
                              const std::function<Traffic::SignalAspect(int, int)>& aspectOf)
    {
        drawCalls_ = 0;
        if (heads_.empty() || !effect_) {
            return;
        }
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;

        // Every lens dark first, in one draw.
        if (darkLenses_ && frustum.Intersects(darkLenses_->Sphere())) {
            device.setBlendStateProperty(BlendState::NonPremultiplied);
            effect_->setTextureProperty(lensTexture_.get());
            effect_->setAlphaProperty(1.0f);
            effect_->setDiffuseColorProperty(Vector3(0.07f, 0.07f, 0.08f));
            ApplyAll(*effect_, device, *darkLenses_);
            ++drawCalls_;
        }

        // Then the lit ones. A lamp is brighter than the daylight around it, so it keeps most of
        // its colour whatever the hour; the halo behind it fades in as the light goes.
        const Vector3 colours[3] = {Vector3(1.00f, 0.13f, 0.09f), Vector3(1.00f, 0.66f, 0.06f), Vector3(0.15f, 0.95f, 0.35f)};
        const float haloStrength = 0.25f + 0.75f * rig.LampFactor();
        for (const Head& head : heads_) {
            const Traffic::SignalAspect aspect = aspectOf ? aspectOf(head.intersection, head.group) : Traffic::SignalAspect::Green;
            if (Vector3::Distance(head.centre, cameraPosition) > kSignalRange) {
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                if (!LensLit(aspect, i) || !head.lens[i] || !frustum.Intersects(head.lens[i]->Sphere())) {
                    continue;
                }
                device.setBlendStateProperty(BlendState::NonPremultiplied);
                effect_->setTextureProperty(lensTexture_.get());
                effect_->setDiffuseColorProperty(colours[i]);
                ApplyAll(*effect_, device, *head.lens[i]);
                ++drawCalls_;
                if (head.glow[i]) {
                    device.setBlendStateProperty(BlendState::Additive);
                    effect_->setTextureProperty(glowTexture_.get());
                    effect_->setDiffuseColorProperty(colours[i] * (0.45f * haloStrength));
                    ApplyAll(*effect_, device, *head.glow[i]);
                    ++drawCalls_;
                }
            }
        }

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    }
}
