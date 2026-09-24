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

        void DrawMesh(Effect& effect, GraphicsDevice& device, const GpuMesh& mesh)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
            }
        }

        std::unique_ptr<GpuMesh> Box(GraphicsDevice& device, const Vector3& mn, const Vector3& mx)
        {
            MeshData m;
            m.AddBox(mn, mx, 1.0f);
            return GpuMesh::Create(device, m, VertexLayout::PositionNormalTexture);
        }
    }

    PedestrianRenderer::PedestrianRenderer(GraphicsDevice& device)
    {
        // Built standing on the origin, facing -Z; legs and arms hang from their joints so a
        // rotation about the joint swings them.
        torso_ = Box(device, Vector3(-0.19f, kHip - 0.02f, -0.11f), Vector3(0.19f, kShoulder + 0.06f, 0.11f));
        leg_ = Box(device, Vector3(-0.075f, 0.07f, -0.08f), Vector3(0.075f, kHip, 0.08f));
        shoe_ = Box(device, Vector3(-0.07f, 0.0f, -0.15f), Vector3(0.07f, 0.08f, 0.07f));
        arm_ = Box(device, Vector3(-0.055f, kShoulder - 0.64f, -0.06f), Vector3(0.055f, kShoulder, 0.06f));
        MeshData head;
        CarBody::AddEllipsoid(head, Vector3(0.0f, 1.61f, 0.0f), Vector3(0.095f, 0.12f, 0.105f), 6, 10);
        head_ = GpuMesh::Create(device, head, VertexLayout::PositionNormalTexture);
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
        if (people.empty()) return;
        static const std::array<Vector3, 8> shirts = {Vector3(0.10f, 0.14f, 0.32f), Vector3(0.55f, 0.08f, 0.08f), Vector3(0.82f, 0.82f, 0.80f),
                                                      Vector3(0.16f, 0.34f, 0.18f), Vector3(0.40f, 0.40f, 0.42f), Vector3(0.78f, 0.62f, 0.16f),
                                                      Vector3(0.28f, 0.18f, 0.40f), Vector3(0.62f, 0.44f, 0.30f)};
        static const std::array<Vector3, 5> trousers = {Vector3(0.08f, 0.10f, 0.20f), Vector3(0.06f, 0.06f, 0.07f), Vector3(0.56f, 0.50f, 0.38f),
                                                        Vector3(0.30f, 0.30f, 0.32f), Vector3(0.18f, 0.24f, 0.40f)};
        static const std::array<Vector3, 4> skins = {Vector3(0.86f, 0.68f, 0.56f), Vector3(0.78f, 0.58f, 0.44f), Vector3(0.92f, 0.76f, 0.64f),
                                                     Vector3(0.58f, 0.40f, 0.28f)};
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
            const Vector3 shirt = shirts[p.look % shirts.size()];
            const Vector3 legs = trousers[(p.look / 8u) % trousers.size()];
            const Vector3 skin = skins[(p.look / 64u) % skins.size()];
            const auto draw = [&](const GpuMesh& mesh, const Matrix& world, const Vector3& colour) {
                effect_->setWorldProperty(world);
                effect_->setDiffuseColorProperty(colour);
                DrawMesh(*effect_, device, mesh);
            };
            draw(*torso_, body, shirt);
            draw(*head_, body, skin);
            for (const float side : {-1.0f, 1.0f}) {
                const float angle = swing * side;
                const Matrix leg = Matrix::CreateTranslation(0.0f, -kHip, 0.0f) * Matrix::CreateRotationX(angle) *
                                   Matrix::CreateTranslation(side * 0.10f, kHip, 0.0f) * body;
                draw(*leg_, leg, legs);
                draw(*shoe_, leg, Vector3(0.07f, 0.06f, 0.05f));
                const Matrix arm = Matrix::CreateTranslation(0.0f, -kShoulder, 0.0f) * Matrix::CreateRotationX(-angle * 0.8f) *
                                   Matrix::CreateTranslation(side * 0.25f, kShoulder, 0.0f) * body;
                draw(*arm_, arm, shirt);
            }
            ++drawn_;
        }
    }
}
