// WorldRenderer scenery construction: buildings, props, lamp geometry, tree cards and signs.
// Existing generation and material batching algorithms are kept in their original form.
#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Render/BuildingGenerator.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Render/PropGenerator.hpp"
#include "CarSim/Render/SignGenerator.hpp"
#include "CarSim/Render/VegetationGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

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
            wallTextures_.push_back(UploadTexture(device, i == BuildingPalette::kStoneWall ? Textures::Masonry(256, BuildingPalette::Wall(i), 31u)
                                                                                          : Textures::Plaster(256, BuildingPalette::Wall(i), 20u + static_cast<unsigned>(i)),
                                                  true));
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
                              const Vector3& emissive = Vector3(0.0f, 0.0f, 0.0f), const float cullDistance = 0.0f,
                              const Vector3& nightEmissive = Vector3(0.0f, 0.0f, 0.0f)) {
            if (m.TriangleCount() == 0) return;
            ObjectBatch b;
            b.nightEmissive = nightEmissive;
            b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionNormalTexture);
            b.texture = texture;
            b.diffuse = diffuse;
            b.specular = specular;
            b.specularPower = power;
            b.emissive = emissive;
            b.cullDistance = cullDistance;
            objectBatches_.push_back(std::move(b));
        };
        const Vector3 noGlow(0.0f, 0.0f, 0.0f);
        constexpr float kDetailRangeM = 420.0f;   // frames, gutters, reveal lines: invisible beyond this
        const Vector3 one(1.0f, 1.0f, 1.0f);
        const Vector3 matte(0.04f, 0.04f, 0.04f);
        for (auto& [key, bm] : buildingChunks) {
            for (int i = 0; i < BuildingPalette::kWallColours; ++i) {
                push(bm.walls[static_cast<std::size_t>(i)], wallTextures_[static_cast<std::size_t>(i)].get(), one, matte, 6.0f);
            }
            for (int i = 0; i < BuildingPalette::kRoofColours; ++i) {
                const std::size_t before = objectBatches_.size();
                push(bm.roofs[static_cast<std::size_t>(i)], roofTextures_[static_cast<std::size_t>(i)].get(), one, Vector3(0.10f, 0.10f, 0.10f), 12.0f);
                if (objectBatches_.size() > before) objectBatches_.back().roof = true;
            }
            push(bm.windows, windowTexture_.get(), one, Vector3(0.6f, 0.6f, 0.6f), 40.0f);
            push(bm.glassDark, white_.get(), Vector3(0.20f, 0.25f, 0.30f), Vector3(0.8f, 0.8f, 0.8f), 60.0f);
            // The same windows with the light on: warm emissive that fades in after sunset.
            push(bm.windowsLit, windowTexture_.get(), one, Vector3(0.6f, 0.6f, 0.6f), 40.0f, noGlow, 0.0f, Vector3(0.62f, 0.50f, 0.30f));
            push(bm.glassLit, white_.get(), Vector3(0.20f, 0.25f, 0.30f), Vector3(0.8f, 0.8f, 0.8f), 60.0f, noGlow, 0.0f,
                 Vector3(0.50f, 0.42f, 0.26f));
            push(bm.trim, white_.get(), Vector3(0.28f, 0.22f, 0.18f), matte, 6.0f);
            push(bm.frames, white_.get(), Vector3(0.90f, 0.89f, 0.84f), Vector3(0.2f, 0.2f, 0.2f), 12.0f, noGlow, kDetailRangeM);
            push(bm.metal, white_.get(), Vector3(0.52f, 0.54f, 0.57f), Vector3(0.5f, 0.5f, 0.5f), 30.0f, noGlow, kDetailRangeM);
            push(bm.dark, white_.get(), Vector3(0.05f, 0.05f, 0.05f), matte, 6.0f, noGlow, kDetailRangeM);
            push(bm.concrete, concrete_.get(), one, matte, 6.0f);
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
            push(pm.hedge, grass_.get(), Vector3(0.26f, 0.38f, 0.17f), matte, 6.0f);
        }
        for (auto& [key, m] : trunkChunks) {
            const std::size_t before = objectBatches_.size();
            push(m, barkTexture_.get(), one, matte, 6.0f, noGlow, 700.0f);
            if (objectBatches_.size() > before) objectBatches_.back().treeTrunk = true;
        }
        stats_.objectBatchesTotal = static_cast<int>(objectBatches_.size());
    }


    void WorldRenderer::BuildLampLights(GraphicsDevice& device)
    {
        // Soft radial falloff, used both for the pool on the ground and for the glow around the
        // lantern. Additive, so black is transparent.
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

        glowEffect_ = std::make_unique<BasicEffect>(device);
        glowEffect_->setLightingEnabledProperty(false);
        glowEffect_->setTextureEnabledProperty(true);
        glowEffect_->setVertexColorEnabledProperty(true);
        glowEffect_->setFogEnabledProperty(false);

        const auto& ground = world_.Ground();
        std::map<std::pair<int, int>, MeshData> chunks;
        constexpr float kPoolRadiusM = 8.5f;
        constexpr int kPoolCells = 6;          // grid so the pool follows the camber and the kerb
        constexpr float kGlowRadiusM = 1.1f;
        for (const auto& prop : world_.Objects().Props()) {
            if (prop.type != Map::PropType::Lamp) {
                continue;
            }
            const float s = prop.scale;
            const float sinH = std::sin(prop.headingRad);
            const float cosH = std::cos(prop.headingRad);
            // The lantern hangs on an arm reaching 1.35 s metres along the prop's local +z.
            const float armZ = 1.35f * s;
            const Vector3 lantern(prop.position.X + sinH * armZ, prop.position.Y + 6.93f * s, prop.position.Z + cosH * armZ);
            lanterns_.push_back(lantern);
            MeshData& mesh = chunks[ChunkKey(lantern.X, lantern.Z)];

            // Ground pool: a grid centred under the lantern, laid on the ground surface and
            // faded out at the rim by the texture.
            const float radius = kPoolRadiusM * s;
            for (int j = 0; j < kPoolCells; ++j) {
                for (int i = 0; i < kPoolCells; ++i) {
                    const float u0 = static_cast<float>(i) / kPoolCells;
                    const float u1 = static_cast<float>(i + 1) / kPoolCells;
                    const float v0 = static_cast<float>(j) / kPoolCells;
                    const float v1 = static_cast<float>(j + 1) / kPoolCells;
                    const auto corner = [&](const float u, const float v) {
                        const float x = lantern.X + (u * 2.0f - 1.0f) * radius;
                        const float z = lantern.Z + (v * 2.0f - 1.0f) * radius;
                        return Vector3(x, ground.HeightAt(x, z) + 0.05f, z);
                    };
                    // Wound like the terrain grid so the pool is not culled from above.
                    mesh.AddQuad(corner(u0, v0), corner(u0, v1), corner(u1, v1), corner(u1, v0), Vector3(0.0f, 1.0f, 0.0f),
                                 Vector2(u0, v0), Vector2(u0, v1), Vector2(u1, v1), Vector2(u1, v0), Color(115, 108, 96, 255));
                }
            }
            // Lantern glow: two crossed vertical cards, so it reads from any direction without
            // per-frame billboarding.
            const float g = kGlowRadiusM * s;
            for (int axis = 0; axis < 2; ++axis) {
                const float ax = axis == 0 ? cosH : -sinH;
                const float az = axis == 0 ? -sinH : -cosH;
                const Vector3 right(ax * g, 0.0f, az * g);
                const Vector3 up(0.0f, g, 0.0f);
                const Vector3 n(-az, 0.0f, ax);
                mesh.AddQuad(lantern - right - up, lantern + right - up, lantern + right + up, lantern - right + up, n,
                             Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Color(255, 244, 222, 255));
            }
        }
        for (auto& [key, mesh] : chunks) {
            if (mesh.TriangleCount() == 0) continue;
            lampLights_.push_back(GpuMesh::Create(device, mesh, VertexLayout::PositionColorTexture));
        }
    }

    void WorldRenderer::BuildTrees(GraphicsDevice& device)
    {
        for (int i = 0; i < VegetationGenerator::kSpeciesCount; ++i) {
            const auto species = static_cast<Map::TreeSpecies>(i);
            const unsigned seed = 100u + static_cast<unsigned>(i);
            Image card = VegetationGenerator::CardAtlasTexture(species, seed);
            DilateColour(card, 8);
            Image winter = VegetationGenerator::WinterAtlasTexture(card, species, seed);
            DilateColour(winter, 8);
            treeCards_.push_back(UploadTexture(device, card, true));
            treeSummerCards_.push_back(std::move(card));
            treeWinterCards_.push_back(std::move(winter));
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

    void WorldRenderer::SetSnow(const float cover)
    {
        snow_ = std::clamp(cover, 0.0f, 1.0f);
        if (treeCards_.empty()) return;
        // Snow settles and melts over minutes. Upload only at small visible increments,
        // avoiding both a seasonal pop and an atlas upload on every frame.
        if (std::fabs(snow_ - treeAtlasSnow_) < 0.04f &&
            !(snow_ == 0.0f && treeAtlasSnow_ != 0.0f) &&
            !(snow_ == 1.0f && treeAtlasSnow_ != 1.0f)) return;
        for (std::size_t i = 0; i < treeCards_.size(); ++i) {
            const Image blended = VegetationGenerator::BlendSeasonalAtlases(treeSummerCards_[i], treeWinterCards_[i], snow_);
            UpdateTexture(*treeCards_[i], blended);
        }
        treeAtlasSnow_ = snow_;
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

}
