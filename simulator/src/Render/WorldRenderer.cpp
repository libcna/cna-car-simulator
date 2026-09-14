#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Render/BuildingGenerator.hpp"
#include "CarSim/Render/PropGenerator.hpp"
#include "CarSim/Render/RoadMeshBuilder.hpp"
#include "CarSim/Render/SignGenerator.hpp"
#include "CarSim/Render/VegetationGenerator.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <algorithm>
#include <cmath>
#include <map>

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

    WorldRenderer::WorldRenderer(GraphicsDevice& device, const LightingRig& rig, const Map::MapWorld& world, const BitmapFont* signFont)
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
        BuildObjects(device);
        BuildTrees(device);
        BuildSigns(device, signFont);

        treeEffect_ = std::make_unique<AlphaTestEffect>(device);
        treeEffect_->setAlphaFunctionProperty(CompareFunction::Greater);
        treeEffect_->setReferenceAlphaProperty(110);
        treeEffect_->setVertexColorEnabledProperty(true);
        treeEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        treeEffect_->setFogEnabledProperty(true);
        treeEffect_->setFogColorProperty(rig.fogColor);
        treeEffect_->setFogStartProperty(rig.fogStart);
        treeEffect_->setFogEndProperty(rig.fogEnd);

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
        // Builds one chunk mesh with the given vertex step (1, 2, 4); the last row/column of a
        // chunk always lands on the chunk edge so neighbours share their boundary vertices.
        const auto build = [&](const int cx, const int cz, const int x1, const int z1, const int step) {
            MeshData mesh;
            std::vector<int> xs;
            std::vector<int> zs;
            for (int x = cx; x < x1; x += step) xs.push_back(x);
            xs.push_back(x1);
            for (int z = cz; z < z1; z += step) zs.push_back(z);
            zs.push_back(z1);
            const int w = static_cast<int>(xs.size());
            for (const int z : zs) {
                for (const int x : xs) {
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
            for (int zi = 0; zi + 1 < static_cast<int>(zs.size()); ++zi) {
                for (int xi = 0; xi + 1 < w; ++xi) {
                    const std::uint32_t i00 = static_cast<std::uint32_t>(zi * w + xi);
                    const std::uint32_t i10 = i00 + 1;
                    const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(w);
                    const std::uint32_t i11 = i01 + 1;
                    // Diagonal (0,0)-(1,1) to match TerrainField::Height; counter-clockwise from above.
                    mesh.AddTriangle(i00, i01, i11);
                    mesh.AddTriangle(i00, i11, i10);
                }
            }
            return GpuMesh::Create(device, mesh, VertexLayout::PositionNormalDualTexture);
        };
        for (int cz = 0; cz + 1 < rows; cz += kChunkCells) {
            for (int cx = 0; cx + 1 < cols; cx += kChunkCells) {
                const int x1 = std::min(cols - 1, cx + kChunkCells);
                const int z1 = std::min(rows - 1, cz + kChunkCells);
                TerrainChunk chunk;
                chunk.lod0 = build(cx, cz, x1, z1, 1);
                chunk.lod1 = build(cx, cz, x1, z1, 2);
                chunk.lod2 = build(cx, cz, x1, z1, 4);
                if (chunk.lod0) {
                    chunk.centre = chunk.lod0->Sphere().Center;
                    chunk.radius = chunk.lod0->Sphere().Radius;
                }
                terrainChunks_.push_back(std::move(chunk));
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
        // Pedestrian crossings (V 7) wherever an IP 6 sign stands.
        for (const auto& sign : world_.Objects().Signs()) {
            if (sign.spec && sign.spec->code == "IP6") {
                builder.BuildCrossing(Microsoft::Xna::Framework::Vector2(sign.position.X, sign.position.Z), markings);
            }
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

    namespace
    {
        constexpr float kObjectChunkM = 256.0f;

        std::pair<int, int> ChunkKey(const float x, const float z)
        {
            return {static_cast<int>(std::floor(x / kObjectChunkM)), static_cast<int>(std::floor(z / kObjectChunkM))};
        }

        /// Spreads opaque colours into transparent texels so mip levels do not darken at the edges.
        void DilateColour(Image& img, const int passes)
        {
            for (int pass = 0; pass < passes; ++pass) {
                Image copy = img;
                for (int y = 0; y < img.Height(); ++y) {
                    for (int x = 0; x < img.Width(); ++x) {
                        if (copy.At(x, y).getAProperty() != 0) continue;
                        int r = 0, g = 0, b = 0, n = 0;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int sx = x + dx;
                                const int sy = y + dy;
                                if (sx < 0 || sy < 0 || sx >= img.Width() || sy >= img.Height()) continue;
                                const Color& c = copy.At(sx, sy);
                                if (c.getAProperty() == 0 && !(c.getRProperty() | c.getGProperty() | c.getBProperty())) continue;
                                r += static_cast<int>(c.getRProperty());
                                g += static_cast<int>(c.getGProperty());
                                b += static_cast<int>(c.getBProperty());
                                ++n;
                            }
                        }
                        if (n > 0) {
                            img.At(x, y) = Color(r / n, g / n, b / n, 0);
                        }
                    }
                }
            }
        }
    }

    void WorldRenderer::BuildObjects(GraphicsDevice& device)
    {
        // Textures per material.
        for (int i = 0; i < BuildingPalette::kWallColours; ++i) {
            wallTextures_.push_back(UploadTexture(device, Textures::Plaster(256, BuildingPalette::Wall(i), 20u + static_cast<unsigned>(i)), true));
        }
        for (int i = 0; i < BuildingPalette::kRoofColours; ++i) {
            roofTextures_.push_back(UploadTexture(device, Textures::RoofTiles(256, BuildingPalette::Roof(i), 40u + static_cast<unsigned>(i)), true));
        }
        windowTexture_ = UploadTexture(device, BuildingGenerator::WindowTexture(128, 3u), true);
        woodTexture_ = UploadTexture(device, Textures::Bark(128, 9u), true);
        barkTexture_ = UploadTexture(device, Textures::Bark(256, 5u), true);

        const auto& objects = world_.Objects();
        std::map<std::pair<int, int>, BuildingMeshes> buildingChunks;
        std::map<std::pair<int, int>, PropMeshes> propChunks;
        std::map<std::pair<int, int>, MeshData> trunkChunks;
        for (const auto& b : objects.Buildings()) {
            BuildingGenerator::Generate(b, buildingChunks[ChunkKey(b.position.X, b.position.Z)]);
        }
        for (const auto& p : objects.Props()) {
            PropGenerator::Generate(p, propChunks[ChunkKey(p.position.X, p.position.Z)]);
        }
        for (const auto& t : objects.Trees()) {
            VegetationGenerator::AppendTrunk(t, trunkChunks[ChunkKey(t.position.X, t.position.Z)]);
        }
        const auto push = [&](MeshData& m, Texture2D* texture, const Vector3& diffuse, const Vector3& specular, const float power,
                              const Vector3& emissive = Vector3(0.0f, 0.0f, 0.0f)) {
            if (m.TriangleCount() == 0) return;
            ObjectBatch b;
            b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionNormalTexture);
            b.texture = texture;
            b.diffuse = diffuse;
            b.specular = specular;
            b.specularPower = power;
            b.emissive = emissive;
            objectBatches_.push_back(std::move(b));
        };
        const Vector3 one(1.0f, 1.0f, 1.0f);
        const Vector3 matte(0.04f, 0.04f, 0.04f);
        for (auto& [key, bm] : buildingChunks) {
            for (int i = 0; i < BuildingPalette::kWallColours; ++i) {
                push(bm.walls[static_cast<std::size_t>(i)], wallTextures_[static_cast<std::size_t>(i)].get(), one, matte, 6.0f);
            }
            for (int i = 0; i < BuildingPalette::kRoofColours; ++i) {
                push(bm.roofs[static_cast<std::size_t>(i)], roofTextures_[static_cast<std::size_t>(i)].get(), one, Vector3(0.10f, 0.10f, 0.10f), 12.0f);
            }
            push(bm.windows, windowTexture_.get(), one, Vector3(0.6f, 0.6f, 0.6f), 40.0f);
            push(bm.glassDark, white_.get(), Vector3(0.20f, 0.25f, 0.30f), Vector3(0.8f, 0.8f, 0.8f), 60.0f);
            push(bm.trim, white_.get(), Vector3(0.28f, 0.22f, 0.18f), matte, 6.0f);
        }
        for (auto& [key, pm] : propChunks) {
            push(pm.metal, white_.get(), Vector3(0.50f, 0.52f, 0.54f), Vector3(0.5f, 0.5f, 0.5f), 30.0f);
            push(pm.wood, woodTexture_.get(), Vector3(0.9f, 0.78f, 0.62f), matte, 6.0f);
            push(pm.concrete, concrete_.get(), one, matte, 6.0f);
            push(pm.white, white_.get(), Vector3(0.92f, 0.92f, 0.90f), Vector3(0.2f, 0.2f, 0.2f), 12.0f);
            push(pm.black, white_.get(), Vector3(0.05f, 0.05f, 0.05f), Vector3(0.2f, 0.2f, 0.2f), 12.0f);
            push(pm.reflectorOrange, white_.get(), Vector3(1.0f, 0.45f, 0.05f), Vector3(0.6f, 0.6f, 0.6f), 40.0f, Vector3(0.45f, 0.18f, 0.0f));
            push(pm.reflectorWhite, white_.get(), Vector3(0.95f, 0.95f, 0.95f), Vector3(0.6f, 0.6f, 0.6f), 40.0f, Vector3(0.35f, 0.35f, 0.35f));
            push(pm.glass, white_.get(), Vector3(0.22f, 0.27f, 0.32f), Vector3(0.9f, 0.9f, 0.9f), 70.0f);
            push(pm.red, white_.get(), Vector3(0.75f, 0.08f, 0.06f), Vector3(0.3f, 0.3f, 0.3f), 20.0f);
        }
        for (auto& [key, m] : trunkChunks) {
            push(m, barkTexture_.get(), one, matte, 6.0f);
        }
        stats_.objectBatchesTotal = static_cast<int>(objectBatches_.size());
    }

    void WorldRenderer::BuildTrees(GraphicsDevice& device)
    {
        for (int i = 0; i < VegetationGenerator::kSpeciesCount; ++i) {
            Image card = VegetationGenerator::CardTexture(static_cast<Map::TreeSpecies>(i), 256, 512, 100u + static_cast<unsigned>(i));
            DilateColour(card, 8);
            treeCards_.push_back(UploadTexture(device, card, true));
        }
        std::map<std::pair<int, int>, std::array<MeshData, VegetationGenerator::kSpeciesCount>> chunks;
        for (const auto& t : world_.Objects().Trees()) {
            VegetationGenerator::AppendTree(t, chunks[ChunkKey(t.position.X, t.position.Z)][static_cast<std::size_t>(t.species)]);
        }
        for (auto& [key, perSpecies] : chunks) {
            for (int i = 0; i < VegetationGenerator::kSpeciesCount; ++i) {
                MeshData& m = perSpecies[static_cast<std::size_t>(i)];
                if (m.TriangleCount() == 0) continue;
                TreeBatch b;
                b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
                b.texture = treeCards_[static_cast<std::size_t>(i)].get();
                treeBatches_.push_back(std::move(b));
            }
        }
        stats_.treeBatchesTotal = static_cast<int>(treeBatches_.size());
    }

    void WorldRenderer::BuildSigns(GraphicsDevice& device, const BitmapFont* font)
    {
        const auto& signs = world_.Objects().Signs();
        if (signs.empty()) {
            return;
        }
        std::unique_ptr<BitmapFont> fallback;
        if (!font) {
            fallback = BitmapFont::CreateBuiltin(device);
            font = fallback.get();
        }
        const Image atlas = font->AtlasImage();
        std::map<std::string, std::size_t> faceIndex;
        std::vector<SignFace> faces;
        std::vector<MeshData> faceMeshes;
        std::vector<MeshData> backMeshes;
        MeshData posts;
        for (const auto& sign : signs) {
            const std::string key = SignGenerator::FaceKey(*sign.spec);
            auto it = faceIndex.find(key);
            if (it == faceIndex.end()) {
                it = faceIndex.emplace(key, faces.size()).first;
                faces.push_back(SignGenerator::Face(*sign.spec, *font, atlas));
                faceMeshes.emplace_back();
                backMeshes.emplace_back();
            }
            SignGenerator::AppendSign(sign, faces[it->second], faceMeshes[it->second], backMeshes[it->second], posts);
        }
        for (std::size_t i = 0; i < faces.size(); ++i) {
            Image img = faces[i].image;
            DilateColour(img, 4);
            signTextures_.push_back(UploadTexture(device, img, true));
            TreeBatch b;
            b.mesh = GpuMesh::Create(device, faceMeshes[i], VertexLayout::PositionColorTexture);
            b.texture = signTextures_.back().get();
            signBatches_.push_back(std::move(b));
            Image backImg = faces[i].back;
            DilateColour(backImg, 4);
            signTextures_.push_back(UploadTexture(device, backImg, true));
            TreeBatch bb;
            bb.mesh = GpuMesh::Create(device, backMeshes[i], VertexLayout::PositionColorTexture);
            bb.texture = signTextures_.back().get();
            signBatches_.push_back(std::move(bb));
        }
        if (posts.TriangleCount() > 0) {
            ObjectBatch b;
            b.mesh = GpuMesh::Create(device, posts, VertexLayout::PositionNormalTexture);
            b.texture = white_.get();
            b.diffuse = Vector3(0.52f, 0.54f, 0.56f);
            b.specular = Vector3(0.4f, 0.4f, 0.4f);
            b.specularPower = 24.0f;
            objectBatches_.push_back(std::move(b));
            stats_.objectBatchesTotal = static_cast<int>(objectBatches_.size());
        }
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
        const Vector3 eye = Matrix::Invert(view).getTranslationProperty();
        for (const auto& chunk : terrainChunks_) {
            if (!chunk.lod0) {
                continue;
            }
            const float distance = Vector3::Distance(eye, chunk.centre) - chunk.radius;
            if (distance > terrainCullDistanceM || !frustum.Intersects(chunk.lod0->Sphere())) {
                continue;
            }
            const GpuMesh* mesh = distance < lod1DistanceM ? chunk.lod0.get() : (distance < lod2DistanceM ? chunk.lod1.get() : chunk.lod2.get());
            if (!mesh) mesh = chunk.lod0.get();
            ApplyAll(*terrainEffect_, device, *mesh);
            ++stats_.terrainChunksDrawn;
            ++stats_.drawCalls;
            stats_.triangles += mesh->PrimitiveCount();
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
        // Static objects: buildings, props, trunks (lit, textured).
        device.setRasterizerStateProperty(solid);
        stats_.objectBatchesDrawn = 0;
        for (const auto& b : objectBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            roadEffect_->setTextureProperty(b.texture);
            roadEffect_->setDiffuseColorProperty(b.diffuse);
            roadEffect_->setEmissiveColorProperty(b.emissive);
            roadEffect_->setSpecularColorProperty(b.specular);
            roadEffect_->setSpecularPowerProperty(b.specularPower);
            ApplyAll(*roadEffect_, device, *b.mesh);
            ++stats_.objectBatchesDrawn;
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        roadEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        roadEffect_->setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
        roadEffect_->setSpecularPowerProperty(10.0f);

        // Trees: alpha-tested cards, both windings present, distance culled.
        const Vector3 cameraPosition = Matrix::Invert(view).getTranslationProperty();
        const float treeRange = 1100.0f;
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        treeEffect_->setWorldProperty(Matrix::getIdentityProperty());
        treeEffect_->setViewProperty(view);
        treeEffect_->setProjectionProperty(projection);
        stats_.treeBatchesDrawn = 0;
        for (const auto& b : treeBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            const BoundingSphere& sphere = b.mesh->Sphere();
            if (Vector3::Distance(sphere.Center, cameraPosition) - sphere.Radius > treeRange) {
                continue;
            }
            treeEffect_->setTextureProperty(b.texture);
            ApplyAll(*treeEffect_, device, *b.mesh);
            ++stats_.treeBatchesDrawn;
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        // Sign faces: alpha-tested, both sides drawn (two quads), no distance cull beyond the frustum.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        for (const auto& b : signBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            treeEffect_->setTextureProperty(b.texture);
            ApplyAll(*treeEffect_, device, *b.mesh);
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    }
}
