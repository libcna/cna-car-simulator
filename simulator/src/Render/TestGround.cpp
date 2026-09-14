#include "CarSim/Render/TestGround.hpp"

#include "CarSim/Render/ProceduralTextures.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    TestGround::TestGround(GraphicsDevice& device, const LightingRig& rig)
    {
        grass_ = UploadTexture(device, Textures::Grass(512, 1u), true);
        asphalt_ = UploadTexture(device, Textures::Asphalt(512, 2u), true);
        plaster_ = UploadTexture(device, Textures::Plaster(256, Rgb::FromBytes(228, 214, 180), 3u), true);

        // Grass plane 2 km square, tiled every 6 m.
        {
            MeshData grass;
            const float half = 1000.0f;
            const float tile = half * 2.0f / 6.0f;
            grass.AddQuad(Vector3(-half, 0, half), Vector3(half, 0, half), Vector3(half, 0, -half), Vector3(-half, 0, -half),
                          Vector3(0, 1, 0), Vector2(0, tile), Vector2(tile, tile), Vector2(tile, 0), Vector2(0, 0));
            Batch b;
            b.mesh = GpuMesh::Create(device, grass, VertexLayout::PositionNormalTexture);
            b.texture = grass_.get();
            batches_.push_back(std::move(b));
        }
        // Asphalt strip 7.5 m wide along -z/+z, slightly above the grass to avoid z-fighting.
        {
            MeshData road;
            const float halfW = 3.75f;
            const float length = 1000.0f;
            const float tiles = length * 2.0f / 4.0f;
            road.AddQuad(Vector3(-halfW, 0.01f, length), Vector3(halfW, 0.01f, length), Vector3(halfW, 0.01f, -length), Vector3(-halfW, 0.01f, -length),
                         Vector3(0, 1, 0), Vector2(0, tiles), Vector2(1.9f, tiles), Vector2(1.9f, 0), Vector2(0, 0));
            Batch b;
            b.mesh = GpuMesh::Create(device, road, VertexLayout::PositionNormalTexture);
            b.texture = asphalt_.get();
            batches_.push_back(std::move(b));
        }
        // A row of plastered blocks each side as scale reference.
        {
            MeshData blocks;
            for (int i = -6; i <= 6; ++i) {
                const float z = static_cast<float>(i) * 40.0f;
                blocks.AddBox(Vector3(9.0f, 0.0f, z - 4.0f), Vector3(17.0f, 6.5f, z + 4.0f), 3.0f);
                blocks.AddBox(Vector3(-16.0f, 0.0f, z + 12.0f), Vector3(-9.0f, 5.0f, z + 20.0f), 3.0f);
            }
            Batch b;
            b.mesh = GpuMesh::Create(device, blocks, VertexLayout::PositionNormalTexture);
            b.texture = plaster_.get();
            batches_.push_back(std::move(b));
        }

        effect_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*effect_);
        effect_->setTextureEnabledProperty(true);
        effect_->setSpecularColorProperty(Vector3(0.05f, 0.05f, 0.05f));
        effect_->setSpecularPowerProperty(8.0f);
    }

    void TestGround::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(view);
        effect_->setProjectionProperty(projection);
        for (const auto& b : batches_) {
            effect_->setTextureProperty(b.texture);
            auto& passes = effect_->getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                b.mesh->Draw(device);
            }
        }
    }
}
