#include "CarSim/Render/PedestrianRenderer.hpp"

#include "CarBody.hpp"
#include "CarSim/Render/Image.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <array>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr float kHip = 0.90f;
        constexpr float kShoulder = 1.40f;

        void DrawMesh(Effect& effect, GraphicsDevice& device, const GpuMesh& mesh, int& calls, int& triangles)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
                ++calls;
                triangles += mesh.PrimitiveCount();
            }
        }

    }

    PedestrianRenderer::PedestrianRenderer(GraphicsDevice& device)
    {
        // Built standing on the origin, facing -Z; legs and arms hang from their joints so a
        // rotation about the joint swings them.
        MeshData torso;
        CarBody::AddEllipsoid(torso, Vector3(0.0f, 1.20f, 0.0f), Vector3(0.21f, 0.29f, 0.13f), 7, 10);
        CarBody::AddEllipsoid(torso, Vector3(0.0f, 0.96f, 0.0f), Vector3(0.17f, 0.12f, 0.12f), 5, 10);
        torso_ = GpuMesh::Create(device, torso, VertexLayout::PositionNormalTexture);
        MeshData coat;
        // A longer outer layer changes the outline around the hips; its shoulder volume
        // covers the cylinder join. It replaces the shirt mesh, so the draw count is fixed.
        coat.AddCylinder(Vector3(0.0f, 0.81f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.205f, 0.38f, 12, true);
        CarBody::AddEllipsoid(coat, Vector3(0.0f, 1.20f, 0.0f), Vector3(0.23f, 0.30f, 0.15f), 7, 10);
        coat_ = GpuMesh::Create(device, coat, VertexLayout::PositionNormalTexture);
        MeshData leg;
        CarBody::AddEllipsoid(leg, Vector3(0.0f, 0.48f, 0.0f), Vector3(0.083f, 0.43f, 0.09f), 8, 10);
        leg_ = GpuMesh::Create(device, leg, VertexLayout::PositionNormalTexture);
        MeshData shoe;
        CarBody::AddEllipsoid(shoe, Vector3(0.0f, 0.055f, -0.035f), Vector3(0.09f, 0.055f, 0.145f), 5, 10);
        shoe_ = GpuMesh::Create(device, shoe, VertexLayout::PositionNormalTexture);
        MeshData arm;
        CarBody::AddEllipsoid(arm, Vector3(0.0f, 1.08f, 0.0f), Vector3(0.065f, 0.33f, 0.073f), 7, 9);
        arm_ = GpuMesh::Create(device, arm, VertexLayout::PositionNormalTexture);
        MeshData hand;
        CarBody::AddEllipsoid(hand, Vector3(0.0f, 0.735f, 0.0f), Vector3(0.045f, 0.07f, 0.05f), 5, 8);
        hand_ = GpuMesh::Create(device, hand, VertexLayout::PositionNormalTexture);
        MeshData head;
        CarBody::AddEllipsoid(head, Vector3(0.0f, 1.61f, 0.0f), Vector3(0.095f, 0.12f, 0.105f), 6, 10);
        CarBody::AddEllipsoid(head, Vector3(0.0f, 1.605f, -0.107f), Vector3(0.027f, 0.035f, 0.034f), 4, 7);
        for (const float side : {-1.0f, 1.0f}) {
            CarBody::AddEllipsoid(head, Vector3(side * 0.095f, 1.6f, 0.0f), Vector3(0.025f, 0.038f, 0.018f), 4, 7);
        }
        head.AddCylinder(Vector3(0.0f, 1.43f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.045f, 0.12f, 9, true);
        head_ = GpuMesh::Create(device, head, VertexLayout::PositionNormalTexture);
        MeshData hair;
        CarBody::AddEllipsoid(hair, Vector3(0.0f, 1.715f, 0.005f), Vector3(0.099f, 0.052f, 0.106f), 5, 10);
        hair_ = GpuMesh::Create(device, hair, VertexLayout::PositionNormalTexture);
        MeshData cap;
        CarBody::AddEllipsoid(cap, Vector3(0.0f, 1.735f, 0.0f), Vector3(0.108f, 0.060f, 0.113f), 5, 10);
        cap.AddBox(Vector3(-0.106f, 1.690f, -0.190f), Vector3(0.106f, 1.706f, -0.074f));
        cap_ = GpuMesh::Create(device, cap, VertexLayout::PositionNormalTexture);
        white_ = UploadTexture(device, Image(4, 4, Color(255, 255, 255, 255)), false);
        effect_ = std::make_unique<BasicEffect>(device);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(false);
    }

    void PedestrianRenderer::Draw(GraphicsDevice& device, const std::vector<Traffic::Pedestrian>& people, const Matrix& view,
                                  const Matrix& projection, const BoundingFrustum& frustum, const Vector3& camera, const LightingRig& rig,
                                  const bool mirrored)
    {
        drawn_ = 0;
        drawCalls_ = 0;
        triangles_ = 0;
        if (people.empty()) return;
        static const std::array<Vector3, 8> shirts = {Vector3(0.10f, 0.14f, 0.32f), Vector3(0.55f, 0.08f, 0.08f), Vector3(0.82f, 0.82f, 0.80f),
                                                      Vector3(0.16f, 0.34f, 0.18f), Vector3(0.40f, 0.40f, 0.42f), Vector3(0.78f, 0.62f, 0.16f),
                                                      Vector3(0.28f, 0.18f, 0.40f), Vector3(0.62f, 0.44f, 0.30f)};
        static const std::array<Vector3, 5> coats = {Vector3(0.12f, 0.18f, 0.27f), Vector3(0.24f, 0.17f, 0.13f),
                                                     Vector3(0.23f, 0.28f, 0.22f), Vector3(0.18f, 0.19f, 0.21f),
                                                     Vector3(0.35f, 0.18f, 0.13f)};
        static const std::array<Vector3, 5> trousers = {Vector3(0.08f, 0.10f, 0.20f), Vector3(0.06f, 0.06f, 0.07f), Vector3(0.56f, 0.50f, 0.38f),
                                                        Vector3(0.30f, 0.30f, 0.32f), Vector3(0.18f, 0.24f, 0.40f)};
        static const std::array<Vector3, 4> skins = {Vector3(0.86f, 0.68f, 0.56f), Vector3(0.78f, 0.58f, 0.44f), Vector3(0.92f, 0.76f, 0.64f),
                                                     Vector3(0.58f, 0.40f, 0.28f)};
        static const std::array<Vector3, 5> hairColours = {Vector3(0.10f, 0.08f, 0.07f), Vector3(0.24f, 0.13f, 0.08f),
                                                            Vector3(0.44f, 0.30f, 0.15f), Vector3(0.16f, 0.14f, 0.13f),
                                                            Vector3(0.43f, 0.43f, 0.42f)};
        rig.Apply(*effect_);
        effect_->setTextureEnabledProperty(true);
        effect_->setVertexColorEnabledProperty(false);
        effect_->setTextureProperty(white_.get());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        effect_->setSpecularColorProperty(Vector3(0.03f, 0.03f, 0.03f));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& p : people) {
            if (Vector3::Distance(p.position, camera) > kRangeM) continue;
            if (!frustum.Intersects(BoundingSphere(p.position + Vector3(0.0f, 0.9f, 0.0f), 1.0f))) continue;
            const Matrix body = Matrix::CreateRotationY(p.headingRad) * Matrix::CreateTranslation(p.position);
            const bool walking = p.crossing == -1 || std::fabs(p.lateral - p.crossFrom) > 0.02f;
            const float swing = walking ? 0.45f * std::sin(p.phase) : 0.0f;
            const bool wearsCoat = p.look % 3u == 1u;
            const Vector3 shirt = wearsCoat ? coats[p.look % coats.size()] : shirts[p.look % shirts.size()];
            const Vector3 legs = trousers[(p.look / 8u) % trousers.size()];
            const Vector3 skin = skins[(p.look / 64u) % skins.size()];
            const auto draw = [&](const GpuMesh& mesh, const Matrix& world, const Vector3& colour) {
                effect_->setWorldProperty(world);
                effect_->setDiffuseColorProperty(colour);
                DrawMesh(*effect_, device, mesh, drawCalls_, triangles_);
            };
            draw(wearsCoat ? *coat_ : *torso_, body, shirt);
            draw(*head_, body, skin);
            if (p.look % 7u != 0u) {
                const Vector3 headwear = hairColours[(p.look / 13u) % hairColours.size()];
                draw(p.look % 4u == 2u ? *cap_ : *hair_, body, headwear);
            }
            for (const float side : {-1.0f, 1.0f}) {
                const float angle = swing * side;
                const Matrix leg = Matrix::CreateTranslation(0.0f, -kHip, 0.0f) * Matrix::CreateRotationX(angle) *
                                   Matrix::CreateTranslation(side * 0.10f, kHip, 0.0f) * body;
                draw(*leg_, leg, legs);
                draw(*shoe_, leg, Vector3(0.07f, 0.06f, 0.05f));
                const Matrix arm = Matrix::CreateTranslation(0.0f, -kShoulder, 0.0f) * Matrix::CreateRotationX(-angle * 0.8f) *
                                   Matrix::CreateTranslation(side * 0.20f, kShoulder, 0.0f) * body;
                draw(*arm_, arm, shirt);
                draw(*hand_, arm, skin);
            }
            ++drawn_;
        }
    }
}
