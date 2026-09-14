#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Render/RoadMeshBuilder.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
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
        constexpr int kChunkCells = 32;
        constexpr float kGrassTileM = 5.0f;

        void ApplyAll(Effect& effect, GraphicsDevice& device, const GpuMesh& mesh)
        {
            auto& passes = effect.getCurrentTechniqueProperty()->getPassesProperty();
            for (int i = 0; i < passes.getCountProperty(); ++i) {
                passes[i]->Apply();
                mesh.Draw(device);
            }
        }

        float Clamp01(const float v) { return std::clamp(v, 0.0f, 1.0f); }
    }

    WorldRenderer::WorldRenderer(GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world)
        : world_(world), rig_(rig)
    {
        grass_ = UploadTexture(device, Textures::Grass(512, 1u), true);
        asphalt_ = UploadTexture(device, Textures::Asphalt(512, 2u), true);
        gravel_ = UploadTexture(device, Textures::Gravel(256, 4u), true);
        paving_ = UploadTexture(device, Textures::PavingSlabs(256, 5u), true);
        concrete_ = UploadTexture(device, Textures::Plaster(128, Rgb::FromBytes(176, 174, 168), 6u), true);
        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);

        BuildMacroTexture(device);
        BuildTerrain(device);
        BuildRoads(device);
        BuildIntersections(device);

        terrainEffect_ = std::make_unique<DualTextureEffect>(device);
        terrainEffect_->setVertexColorEnabledProperty(false);
        terrainEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        terrainEffect_->setFogEnabledProperty(true);
        terrainEffect_->setFogColorProperty(rig.fogColor);
        terrainEffect_->setFogStartProperty(rig.fogStart);
        terrainEffect_->setFogEndProperty(rig.fogEnd);
        terrainEffect_->setTextureProperty(grass_.get());
        terrainEffect_->setTexture2Property(macro_.get());

        roadEffect_ = std::make_unique<BasicEffect>(device);
        rig.Apply(*roadEffect_);
        roadEffect_->setTextureEnabledProperty(true);
        roadEffect_->setVertexColorEnabledProperty(false);
        roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
        roadEffect_->setSpecularPowerProperty(10.0f);

        markingState_ = std::make_unique<RasterizerState>();
        markingState_->setCullModeProperty(CullMode::CullCounterClockwiseFace);
        markingState_->setDepthBiasProperty(-0.00002f);
        markingState_->setSlopeScaleDepthBiasProperty(-1.0f);
        markingStateMirrored_ = std::make_unique<RasterizerState>();
        markingStateMirrored_->setCullModeProperty(CullMode::CullClockwiseFace);
        markingStateMirrored_->setDepthBiasProperty(-0.00002f);
        markingStateMirrored_->setSlopeScaleDepthBiasProperty(-1.0f);
    }

    void WorldRenderer::BuildMacroTexture(GraphicsDevice& device)
    {
        // One texel per terrain vertex (capped at 2048): region tint x baked sun lighting x
        // occlusion near roads. DualTextureEffect multiplies detail x macro x 2, so the macro
        // holds roughly half intensity for a neutral result.
        const auto& terrain = world_.Terrain();
        const int width = std::min(2048, terrain.Columns());
        const int height = std::min(2048, terrain.Rows());
        Image macro(width, height);
        const Vector3 toSun = -rig_.sunDirection;
        const float sizeX = terrain.MaxX() - terrain.MinX();
        const float sizeZ = terrain.MaxZ() - terrain.MinZ();
        for (int y = 0; y < height; ++y) {
            const float z = terrain.MinZ() + (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * sizeZ;
            for (int x = 0; x < width; ++x) {
                const float wx = terrain.MinX() + (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * sizeX;
                const Vector3 n = terrain.Normal(wx, z);
                const float lambert = std::max(0.0f, Vector3::Dot(n, toSun));
                float light = 0.42f + 0.62f * lambert;
                // Region tint.
                Rgb tint{0.56f, 0.60f, 0.40f};
                const Map::RegionType region = terrain.RegionAt(wx, z);
                const float variation = Core::Noise::FbmSigned(wx * 0.012f, z * 0.012f, 3, 0.5f, 91u);
                switch (region) {
                    case Map::RegionType::Meadow:
                        tint = Rgb{0.55f + 0.06f * variation, 0.62f + 0.05f * variation, 0.36f};
                        break;
                    case Map::RegionType::Town:
                        tint = Rgb{0.56f, 0.60f, 0.40f};
                        break;
                    case Map::RegionType::Forest:
                        tint = Rgb{0.36f, 0.34f, 0.24f};
                        light *= 0.78f;   // canopy shade
                        break;
                    case Map::RegionType::Orchard:
                        tint = Rgb{0.52f, 0.60f, 0.36f};
                        break;
                    case Map::RegionType::Field: {
                        const Map::RegionSpec* spec = terrain.RegionSpecAt(wx, z);
                        const std::string crop = spec ? spec->crop : "stubble";
                        const unsigned seed = spec ? spec->seed : 1u;
                        const float angle = static_cast<float>(seed % 7) * 0.45f;
                        const float along = wx * std::cos(angle) + z * std::sin(angle);
                        const float furrow = 0.5f + 0.5f * std::sin(along * 2.0f * 3.14159265f / 3.0f);
                        if (crop == "wheat") tint = Rgb{0.80f, 0.70f, 0.36f};
                        else if (crop == "rapeseed") tint = Rgb{0.78f, 0.74f, 0.22f};
                        else if (crop == "maize") tint = Rgb{0.44f, 0.56f, 0.28f};
                        else if (crop == "ploughed") tint = Rgb{0.42f, 0.32f, 0.24f};
                        else tint = Rgb{0.74f, 0.66f, 0.42f};
                        const float f = 0.9f + 0.1f * furrow;
                        tint = Rgb{tint.r * f, tint.g * f, tint.b * f};
                        break;
                    }
                }
                // Road verges: bare soil right next to the pavement.
                const int col = std::clamp(static_cast<int>((wx - terrain.MinX()) / terrain.CellSize() + 0.5f), 0, terrain.Columns() - 1);
                const int row = std::clamp(static_cast<int>((z - terrain.MinZ()) / terrain.CellSize() + 0.5f), 0, terrain.Rows() - 1);
                const float roadDistance = terrain.RoadDistanceAtVertex(col, row);
                if (roadDistance < 3.0f) {
                    const float t = Clamp01((roadDistance + 1.0f) / 4.0f);
                    tint = Rgb{tint.r * (0.75f + 0.25f * t) + 0.10f * (1.0f - t), tint.g * (0.72f + 0.28f * t) + 0.06f * (1.0f - t), tint.b * (0.7f + 0.3f * t) + 0.03f * (1.0f - t)};
                }
                const float k = 0.5f * light;
                macro.Set(x, y, Rgb{Clamp01(tint.r * k * 1.15f), Clamp01(tint.g * k * 1.15f), Clamp01(tint.b * k * 1.15f)});
            }
        }
        macro_ = UploadTexture(device, macro, true);
    }

    void WorldRenderer::BuildTerrain(GraphicsDevice& device)
    {
        const auto& terrain = world_.Terrain();
        const int cols = terrain.Columns();
        const int rows = terrain.Rows();
        const float cell = terrain.CellSize();
        const float sizeX = terrain.MaxX() - terrain.MinX();
        const float sizeZ = terrain.MaxZ() - terrain.MinZ();
        for (int cz = 0; cz + 1 < rows; cz += kChunkCells) {
            for (int cx = 0; cx + 1 < cols; cx += kChunkCells) {
                const int x1 = std::min(cols - 1, cx + kChunkCells);
                const int z1 = std::min(rows - 1, cz + kChunkCells);
                MeshData mesh;
                const int w = x1 - cx + 1;
                for (int z = cz; z <= z1; ++z) {
                    for (int x = cx; x <= x1; ++x) {
                        const float wx = terrain.MinX() + static_cast<float>(x) * cell;
                        const float wz = terrain.MinZ() + static_cast<float>(z) * cell;
                        MeshVertex v;
                        v.position = Vector3(wx, terrain.HeightAtVertex(x, z), wz);
                        v.normal = terrain.Normal(wx, wz);
                        v.uv = Vector2(wx / kGrassTileM, wz / kGrassTileM);
                        v.uv2 = Vector2((wx - terrain.MinX()) / sizeX, (wz - terrain.MinZ()) / sizeZ);
                        mesh.AddVertex(v);
                    }
                }
                for (int z = cz; z < z1; ++z) {
                    for (int x = cx; x < x1; ++x) {
                        const std::uint32_t i00 = static_cast<std::uint32_t>((z - cz) * w + (x - cx));
                        const std::uint32_t i10 = i00 + 1;
                        const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(w);
                        const std::uint32_t i11 = i01 + 1;
                        // Diagonal (0,0)-(1,1) to match TerrainField::Height; counter-clockwise from above.
                        mesh.AddTriangle(i00, i01, i11);
                        mesh.AddTriangle(i00, i11, i10);
                    }
                }
                terrainChunks_.push_back(GpuMesh::Create(device, mesh, VertexLayout::PositionNormalDualTexture));
            }
        }
        stats_.terrainChunksTotal = static_cast<int>(terrainChunks_.size());
    }

    void WorldRenderer::BuildRoads(GraphicsDevice& device)
    {
        const RoadMeshBuilder builder(world_.Roads());
        for (const auto& piece : world_.Roads().Pieces()) {
            RoadPieceMeshes meshes = builder.BuildPiece(piece);
            const auto& road = world_.Roads().Roads()[static_cast<std::size_t>(piece.road)];
            const Surface pavedSurface = road.profile.surface == Sim::SurfaceType::Gravel || road.profile.surface == Sim::SurfaceType::Dirt
                                             ? Surface::Gravel : Surface::Asphalt;
            const auto push = [&](MeshData& m, Surface s) {
                if (m.TriangleCount() == 0) return;
                Batch b;
                b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionNormalTexture);
                b.surface = s;
                roadBatches_.push_back(std::move(b));
            };
            push(meshes.paved, pavedSurface);
            push(meshes.shoulder, Surface::Gravel);
            push(meshes.sidewalk, Surface::Paving);
            push(meshes.kerb, Surface::Concrete);
            push(meshes.markings, Surface::Marking);
        }
        stats_.roadBatchesTotal = static_cast<int>(roadBatches_.size());
    }

    void WorldRenderer::BuildIntersections(GraphicsDevice& device)
    {
        const RoadMeshBuilder builder(world_.Roads());
        MeshData asphalt;
        MeshData gravel;
        MeshData markings;
        for (const auto& inter : world_.Roads().Intersections()) {
            MeshData& target = inter.surface == Sim::SurfaceType::Gravel || inter.surface == Sim::SurfaceType::Dirt ? gravel : asphalt;
            builder.BuildIntersection(inter, target, markings);
        }
        const auto push = [&](MeshData& m, Surface s) {
            if (m.TriangleCount() == 0) return;
            Batch b;
            b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionNormalTexture);
            b.surface = s;
            roadBatches_.push_back(std::move(b));
        };
        push(asphalt, Surface::Asphalt);
        push(gravel, Surface::Gravel);
        push(markings, Surface::Marking);
        stats_.roadBatchesTotal = static_cast<int>(roadBatches_.size());
    }

    Texture2D* WorldRenderer::TextureFor(const Surface s) const
    {
        switch (s) {
            case Surface::Asphalt: return asphalt_.get();
            case Surface::Gravel: return gravel_.get();
            case Surface::Paving: return paving_.get();
            case Surface::Concrete: return concrete_.get();
            case Surface::Marking: return white_.get();
        }
        return white_.get();
    }

    void WorldRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum,
                             const bool mirrored)
    {
        const RasterizerState& solid = mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise;
        stats_.terrainChunksDrawn = 0;
        stats_.roadBatchesDrawn = 0;
        stats_.drawCalls = 0;
        stats_.triangles = 0;

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(solid);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        device.getSamplerStatesProperty()[1] = SamplerState::LinearClamp;

        terrainEffect_->setWorldProperty(Matrix::getIdentityProperty());
        terrainEffect_->setViewProperty(view);
        terrainEffect_->setProjectionProperty(projection);
        for (const auto& chunk : terrainChunks_) {
            if (!chunk || !frustum.Intersects(chunk->Sphere())) {
                continue;
            }
            ApplyAll(*terrainEffect_, device, *chunk);
            ++stats_.terrainChunksDrawn;
            ++stats_.drawCalls;
            stats_.triangles += chunk->PrimitiveCount();
        }

        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        roadEffect_->setWorldProperty(Matrix::getIdentityProperty());
        roadEffect_->setViewProperty(view);
        roadEffect_->setProjectionProperty(projection);
        for (int pass = 0; pass < 2; ++pass) {
            // Pass 0: surfaces; pass 1: markings with a depth bias.
            const bool markings = pass == 1;
            device.setRasterizerStateProperty(markings ? (mirrored ? *markingStateMirrored_ : *markingState_) : solid);
            roadEffect_->setDiffuseColorProperty(markings ? Vector3(0.92f, 0.92f, 0.90f) : Vector3(1.0f, 1.0f, 1.0f));
            for (const auto& b : roadBatches_) {
                if ((b.surface == Surface::Marking) != markings || !b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                    continue;
                }
                roadEffect_->setTextureProperty(TextureFor(b.surface));
                ApplyAll(*roadEffect_, device, *b.mesh);
                ++stats_.roadBatchesDrawn;
                ++stats_.drawCalls;
                stats_.triangles += b.mesh->PrimitiveCount();
            }
        }
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    }
}
