#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Render/GroundShadowBaker.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/ProceduralTextures.hpp"
#include "CarSim/Render/RoadMeshBuilder.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr int kChunkCells = 32;
        constexpr float kGrassTileM = 7.0f;

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
        cobbles_ = UploadTexture(device, Textures::Cobbles(256, 9u), true);
        concrete_ = UploadTexture(device, Textures::Plaster(128, Rgb::FromBytes(176, 174, 168), 6u), true);
        marking_ = UploadTexture(device, Textures::MarkingPaint(64, 7u), true);
        white_ = UploadTexture(device, Textures::Solid(4, Color(255, 255, 255, 255)), false);

        // Ground shadows of buildings and trees at two texels per terrain cell (capped), baked
        // into the terrain macro and the road vertex colours.
        const auto& terrain = world_.Terrain();
        const int macroW = std::min(2048, std::max(1, 2 * terrain.Columns()));
        const int macroH = std::min(2048, std::max(1, 2 * terrain.Rows()));
        const Image shadow = GroundShadows::Bake(world_, bakeRig_.sunDirection, macroW, macroH);
        shadowSun_ = bakeRig_.sunDirection;
        Image tint(macroW, macroH);
        BuildMacroTexture(device, shadow, tint);
        BuildTerrain(device);
        BuildRoads(device, shadow, tint);
        BuildIntersections(device, shadow);
        BuildPavedAreas(device, shadow);
        BuildObjects(device);
        BuildLampLights(device);
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

        headlightObjectEffect_ = std::make_unique<BasicEffect>(device);
        headlightObjectEffect_->setLightingEnabledProperty(true);
        headlightObjectEffect_->setTextureEnabledProperty(true);
        headlightObjectEffect_->setVertexColorEnabledProperty(false);
        headlightObjectEffect_->setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        headlightObjectEffect_->getDirectionalLight0Property().setEnabledProperty(true);
        headlightObjectEffect_->getDirectionalLight1Property().setEnabledProperty(false);
        headlightObjectEffect_->getDirectionalLight2Property().setEnabledProperty(false);
        headlightObjectEffect_->setFogEnabledProperty(true);
        headlightObjectEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        headlightTreeEffect_ = std::make_unique<AlphaTestEffect>(device);
        headlightTreeEffect_->setAlphaFunctionProperty(CompareFunction::Greater);
        headlightTreeEffect_->setReferenceAlphaProperty(110);
        headlightTreeEffect_->setVertexColorEnabledProperty(true);
        headlightTreeEffect_->setFogEnabledProperty(true);
        headlightTreeEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));

        // Wet-road sheen. Wet asphalt is dark under your wheels and a mirror at the far end of
        // the street, because the sky's reflection climbs with the grazing angle -- and down a
        // road, distance is the grazing angle. A BasicEffect's fog is a distance ramp, so a
        // second additive pass over the road with a black diffuse and the sky as the fog colour
        // puts exactly that sheen on it: nothing near, sky far. No renderer internals, no custom
        // shader, and it costs one extra pass over the road batches only while the road is wet.
        puddleMask_ = UploadTexture(device, Textures::PuddleMask(256, 71u), true);
        snowTexture_ = UploadTexture(device, Textures::SnowLayer(256, 29u), true);
        snowEffect_ = std::make_unique<BasicEffect>(device);
        snowEffect_->setLightingEnabledProperty(false);
        snowEffect_->setTextureEnabledProperty(true);
        snowEffect_->setVertexColorEnabledProperty(false);
        snowEffect_->setFogEnabledProperty(true);
        puddleEffect_ = std::make_unique<BasicEffect>(device);
        puddleEffect_->setLightingEnabledProperty(false);
        puddleEffect_->setTextureEnabledProperty(true);
        puddleEffect_->setVertexColorEnabledProperty(false);
        puddleEffect_->setFogEnabledProperty(true);
        puddleEffect_->setFogColorProperty(Vector3(0.0f, 0.0f, 0.0f));   // additive: fog fades it out
        roadSheenEffect_ = std::make_unique<BasicEffect>(device);
        roadSheenEffect_->setLightingEnabledProperty(false);
        roadSheenEffect_->setTextureEnabledProperty(false);
        roadSheenEffect_->setVertexColorEnabledProperty(false);
        roadSheenEffect_->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        roadSheenEffect_->setFogEnabledProperty(true);

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

        // Roads carry their lighting, wear and ground shadows in the vertex colours.
        roadUnlitEffect_ = std::make_unique<BasicEffect>(device);
        roadUnlitEffect_->setLightingEnabledProperty(false);
        roadUnlitEffect_->setTextureEnabledProperty(true);
        roadUnlitEffect_->setVertexColorEnabledProperty(true);
        roadUnlitEffect_->setFogEnabledProperty(true);
        roadUnlitEffect_->setFogColorProperty(rig.fogColor);
        roadUnlitEffect_->setFogStartProperty(rig.fogStart);
        roadUnlitEffect_->setFogEndProperty(rig.fogEnd);

        markingState_ = std::make_unique<RasterizerState>();
        markingState_->setCullModeProperty(CullMode::CullCounterClockwiseFace);
        markingState_->setDepthBiasProperty(-0.00002f);
        markingState_->setSlopeScaleDepthBiasProperty(-1.0f);
        markingStateMirrored_ = std::make_unique<RasterizerState>();
        markingStateMirrored_->setCullModeProperty(CullMode::CullClockwiseFace);
        markingStateMirrored_->setDepthBiasProperty(-0.00002f);
        markingStateMirrored_->setSlopeScaleDepthBiasProperty(-1.0f);
    }

    void WorldRenderer::ApplyLighting()
    {
        const Vector3 scale = rig_.BakedLightingScale(bakeRig_);
        if (roadEffect_) {
            rig_.Apply(*roadEffect_);
            roadEffect_->setTextureEnabledProperty(true);
            roadEffect_->setVertexColorEnabledProperty(false);
            roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
            roadEffect_->setSpecularPowerProperty(10.0f);
        }
        if (terrainEffect_) {
            terrainEffect_->setDiffuseColorProperty(scale);
            terrainEffect_->setFogColorProperty(rig_.fogColor);
            terrainEffect_->setFogStartProperty(rig_.fogStart);
            terrainEffect_->setFogEndProperty(rig_.fogEnd);
        }
        bakedScale_ = scale;
        lampFactor_ = rig_.LampFactor();
        if (roadUnlitEffect_) {
            roadUnlitEffect_->setDiffuseColorProperty(scale);
            roadUnlitEffect_->setFogColorProperty(rig_.fogColor);
            roadUnlitEffect_->setFogStartProperty(rig_.fogStart);
            roadUnlitEffect_->setFogEndProperty(rig_.fogEnd);
        }
        if (treeEffect_) {
            treeEffect_->setDiffuseColorProperty(scale);
            treeEffect_->setFogColorProperty(rig_.fogColor);
            treeEffect_->setFogStartProperty(rig_.fogStart);
            treeEffect_->setFogEndProperty(rig_.fogEnd);
        }
    }

    struct WorldRenderer::ShadowBakeJob
    {
        Vector3 sun;
        Image macro;
        std::vector<std::vector<Color>> colours;   // per road batch, in batch order
        std::atomic<bool> done{false};
        std::thread worker;
        double seconds = 0.0;
    };

    WorldRenderer::~WorldRenderer()
    {
        if (bakeJob_ && bakeJob_->worker.joinable()) bakeJob_->worker.join();
    }

    void WorldRenderer::UpdateSunShadows(GraphicsDevice& device, const Vector3& sunDirection)
    {
#if defined(__EMSCRIPTEN__)
        // No worker threads in the browser build: the shadows stay as baked at load.
        (void)device;
        (void)sunDirection;
        return;
#else
        if (bakeJob_) {
            if (!bakeJob_->done.load()) return;
            if (bakeJob_->worker.joinable()) bakeJob_->worker.join();
            // Swap the result in: the terrain macro at once, the road batches a few per frame so
            // re-uploading them does not stall a frame.
            if (swapNext_ == 0 && bakeJob_->macro.Width() > 0) {
                macro_ = UploadTexture(device, bakeJob_->macro, true);
                terrainEffect_->setTexture2Property(macro_.get());
            }
            constexpr std::size_t kBatchesPerFrame = 48;
            const std::size_t end = std::min(roadBatches_.size(), swapNext_ + kBatchesPerFrame);
            for (; swapNext_ < end; ++swapNext_) {
                Batch& b = roadBatches_[swapNext_];
                if (!b.source || swapNext_ >= bakeJob_->colours.size() || bakeJob_->colours[swapNext_].empty()) continue;
                MeshData m = *b.source;
                const auto& colours = bakeJob_->colours[swapNext_];
                for (std::size_t i = 0; i < m.vertices.size() && i < colours.size(); ++i) m.vertices[i].color = colours[i];
                b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
            }
            if (swapNext_ >= roadBatches_.size()) {
                std::cout << "shadows: re-baked for the sun at " << std::lround(std::asin(std::clamp(-bakeJob_->sun.Y, -1.0f, 1.0f)) * 57.2958f)
                          << " degrees in " << bakeJob_->seconds << " s\n";
                shadowSun_ = bakeJob_->sun;
                bakeJob_.reset();
                swapNext_ = 0;
            }
            return;
        }
        // Only a sun well up casts the shadows worth moving; below that they are faint and
        // long, and at night the last bake simply stays.
        const Vector3 toSun = -sunDirection;
        if (toSun.Y < 0.06f) return;
        const float turned = std::acos(std::clamp(Vector3::Dot(sunDirection, shadowSun_), -1.0f, 1.0f));
        if (turned < 10.0f * 3.14159265f / 180.0f) return;

        bakeJob_ = std::make_unique<ShadowBakeJob>();
        bakeJob_->sun = sunDirection;
        swapNext_ = 0;
        std::vector<std::pair<std::shared_ptr<const MeshData>, bool>> sources;
        sources.reserve(roadBatches_.size());
        for (const auto& b : roadBatches_) sources.emplace_back(b.source, b.verge);
        ShadowBakeJob* job = bakeJob_.get();
        job->worker = std::thread([this, job, sources = std::move(sources)]() {
            const auto started = std::chrono::steady_clock::now();
            const auto& terrain = world_.Terrain();
            const int macroW = std::min(2048, std::max(1, 2 * terrain.Columns()));
            const int macroH = std::min(2048, std::max(1, 2 * terrain.Rows()));
            const Image shadow = GroundShadows::Bake(world_, job->sun, macroW, macroH);
            Image macro(macroW, macroH);
            Image tint(macroW, macroH);
            ComputeMacro(shadow, macro, tint);
            job->colours.resize(sources.size());
            for (std::size_t i = 0; i < sources.size(); ++i) {
                if (!sources[i].first) continue;
                MeshData m = *sources[i].first;
                BakeRoadColours(m, shadow, sources[i].second ? &tint : nullptr);
                auto& out = job->colours[i];
                out.reserve(m.vertices.size());
                for (const auto& v : m.vertices) out.push_back(v.color);
            }
            job->macro = std::move(macro);
            job->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            job->done.store(true);
        });
#endif
    }

    void WorldRenderer::SetHeadlights(const Vector3& position, const Vector3& forward,
                                      const float intensity, const bool highBeam)
    {
        headlightPosition_ = position;
        headlightForward_ = forward;
        headlightForward_.Y = 0.0f;
        if (headlightForward_.LengthSquared() > 1e-6f) headlightForward_.Normalize();
        headlightIntensity_ = std::clamp(intensity, 0.0f, 1.0f);
        headlightHighBeam_ = highBeam;
    }

    void WorldRenderer::BuildMacroTexture(GraphicsDevice& device, const Image& shadow, Image& tintOut)
    {
        Image macro(shadow.Width(), shadow.Height());
        ComputeMacro(shadow, macro, tintOut);
        macro_ = UploadTexture(device, macro, true);
    }

    void WorldRenderer::ComputeMacro(const Image& shadow, Image& macro, Image& tintOut) const
    {
        // Two texels per terrain cell (capped at 2048): region tint x baked sun lighting x
        // occlusion near roads x ground shadows.
        const auto& terrain = world_.Terrain();
        const int width = shadow.Width();
        const int height = shadow.Height();
        const Vector3 toSun = -bakeRig_.sunDirection;
        const float sizeX = terrain.MaxX() - terrain.MinX();
        const float sizeZ = terrain.MaxZ() - terrain.MinZ();
        // Furrows are only drawn where a texel can hold them: at under two texels per furrow they
        // alias into broad false stripes across the whole field.
        const float texelM = std::max(sizeX / static_cast<float>(width), sizeZ / static_cast<float>(height));
        constexpr float kFurrowM = 3.0f;
        const float furrowShare = Clamp01((kFurrowM / texelM - 2.0f) / 2.0f);
        for (int y = 0; y < height; ++y) {
            const float z = terrain.MinZ() + (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * sizeZ;
            for (int x = 0; x < width; ++x) {
                const float wx = terrain.MinX() + (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * sizeX;
                const Vector3 n = terrain.Normal(wx, z);
                const float lambert = std::max(0.0f, Vector3::Dot(n, toSun));
                float light = 0.40f + 0.66f * lambert;
                // Region tint.
                Rgb tint{0.56f, 0.60f, 0.40f};
                const Map::RegionType region = terrain.RegionAt(wx, z);
                const float variation = Core::Noise::FbmSigned(wx * 0.012f, z * 0.012f, 3, 0.5f, 91u);
                // Medium-scale patches (tens of metres) that the 7 m grass tile cannot carry: lusher
                // and thinner, drier ground. They break up the tile's repeat seen from above.
                const float patch = Core::Noise::FbmSigned(wx / 23.0f, z / 23.0f, 3, 0.55f, 131u);
                const float dryness = Core::Noise::FbmSigned(wx / 61.0f, z / 61.0f, 2, 0.5f, 177u);
                switch (region) {
                    case Map::RegionType::Meadow: {
                        tint = Rgb{0.55f + 0.06f * variation, 0.60f + 0.04f * variation, 0.44f + 0.03f * variation};
                        const float lush = 1.0f + 0.10f * patch;
                        const float dry = Clamp01(dryness * 0.6f) * 0.35f;
                        tint = Rgb{(tint.r * (1.0f - dry) + 0.70f * dry) * lush, (tint.g * (1.0f - dry) + 0.64f * dry) * lush,
                                   (tint.b * (1.0f - dry) + 0.40f * dry) * lush};
                        break;
                    }
                    case Map::RegionType::Town:
                        tint = Rgb{(0.56f + 0.04f * variation) * (1.0f + 0.07f * patch), 0.60f * (1.0f + 0.07f * patch), 0.45f};
                        break;
                    case Map::RegionType::Forest:
                        tint = Rgb{0.38f, 0.35f, 0.25f};
                        light *= 0.72f;   // canopy shade
                        break;
                    case Map::RegionType::Square:
                        tint = Rgb{0.46f, 0.45f, 0.43f};
                        break;
                    case Map::RegionType::Yard:
                        tint = Rgb{0.52f, 0.52f, 0.50f};
                        break;
                    case Map::RegionType::Orchard:
                        tint = Rgb{0.56f * (1.0f + 0.08f * patch), 0.60f * (1.0f + 0.08f * patch), 0.38f};
                        break;
                    case Map::RegionType::Field: {
                        const Map::RegionSpec* spec = terrain.RegionSpecAt(wx, z);
                        const std::string crop = spec ? spec->crop : "stubble";
                        const unsigned seed = spec ? spec->seed : 1u;
                        const float angle = static_cast<float>(seed % 7) * 0.45f;
                        const float along = wx * std::cos(angle) + z * std::sin(angle);
                        const float furrow = 0.5f + 0.5f * std::sin(along * 2.0f * 3.14159265f / 3.0f);
                        if (crop == "wheat") tint = Rgb{0.68f, 0.60f, 0.34f};
                        else if (crop == "rapeseed") tint = Rgb{0.68f, 0.64f, 0.24f};
                        else if (crop == "maize") tint = Rgb{0.42f, 0.52f, 0.28f};
                        else if (crop == "ploughed") tint = Rgb{0.40f, 0.31f, 0.24f};
                        else tint = Rgb{0.64f, 0.58f, 0.40f};
                        // Soil and crop density vary across a field too.
                        const float f = (1.0f - 0.1f * furrowShare + 0.1f * furrowShare * furrow) * (1.0f + 0.06f * patch);
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
                // The effect multiplies detail x macro (no doubling on this renderer), so the
                // macro carries the full lighting; sunlit meadow lands near 0.45 with the grass.
                const float k = light * (static_cast<float>(shadow.At(x, y).getRProperty()) / 255.0f) * 1.12f;
                const Rgb value{Clamp01(tint.r * k), Clamp01(tint.g * k), Clamp01(tint.b * k)};
                macro.Set(x, y, value);
                tintOut.Set(x, y, value);   // what the terrain shows here before the grass detail
            }
        }
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

        // Horizon apron: the terrain grid ends 2 km from the centre, and from any high ground the
        // edge used to read as the world ending in mid-air. A flat skirt carries the edge height
        // and the macro colour (the macro sampler clamps) out to the fog.
        {
            constexpr float kSkirtM = 8000.0f;
            MeshData skirt;
            const auto add = [&](const float x, const float z, const float height, const float u2x, const float u2z) {
                MeshVertex v;
                v.position = Vector3(x, height, z);
                v.normal = Vector3(0.0f, 1.0f, 0.0f);
                v.uv = Vector2(x / kGrassTileM, z / kGrassTileM);
                v.uv2 = Vector2(u2x, u2z);
                return skirt.AddVertex(v);
            };
            const float minX = terrain.MinX(), maxX = terrain.MaxX(), minZ = terrain.MinZ(), maxZ = terrain.MaxZ();
            const int step = 8;   // every eighth terrain vertex is plenty for a flat apron
            for (int x = 0; x + step < cols; x += step) {
                const int xn = std::min(cols - 1, x + step);
                const float x0 = minX + static_cast<float>(x) * cell;
                const float x1 = minX + static_cast<float>(xn) * cell;
                const float u0 = (x0 - minX) / sizeX, u1 = (x1 - minX) / sizeX;
                // North edge (z = minZ): outward is -z.
                skirt.AddQuad(add(x0, minZ - kSkirtM, terrain.HeightAtVertex(x, 0), u0, 0.0f),
                              add(x0, minZ, terrain.HeightAtVertex(x, 0), u0, 0.0f),
                              add(x1, minZ, terrain.HeightAtVertex(xn, 0), u1, 0.0f),
                              add(x1, minZ - kSkirtM, terrain.HeightAtVertex(xn, 0), u1, 0.0f));
                // South edge (z = maxZ): outward is +z.
                skirt.AddQuad(add(x0, maxZ, terrain.HeightAtVertex(x, rows - 1), u0, 1.0f),
                              add(x0, maxZ + kSkirtM, terrain.HeightAtVertex(x, rows - 1), u0, 1.0f),
                              add(x1, maxZ + kSkirtM, terrain.HeightAtVertex(xn, rows - 1), u1, 1.0f),
                              add(x1, maxZ, terrain.HeightAtVertex(xn, rows - 1), u1, 1.0f));
            }
            for (int z = 0; z + step < rows; z += step) {
                const int zn = std::min(rows - 1, z + step);
                const float z0 = minZ + static_cast<float>(z) * cell;
                const float z1 = minZ + static_cast<float>(zn) * cell;
                const float v0 = (z0 - minZ) / sizeZ, v1 = (z1 - minZ) / sizeZ;
                // West edge (x = minX): outward is -x.
                skirt.AddQuad(add(minX - kSkirtM, z0, terrain.HeightAtVertex(0, z), 0.0f, v0),
                              add(minX - kSkirtM, z1, terrain.HeightAtVertex(0, zn), 0.0f, v1),
                              add(minX, z1, terrain.HeightAtVertex(0, zn), 0.0f, v1),
                              add(minX, z0, terrain.HeightAtVertex(0, z), 0.0f, v0));
                // East edge (x = maxX): outward is +x.
                skirt.AddQuad(add(maxX, z0, terrain.HeightAtVertex(cols - 1, z), 1.0f, v0),
                              add(maxX, z1, terrain.HeightAtVertex(cols - 1, zn), 1.0f, v1),
                              add(maxX + kSkirtM, z1, terrain.HeightAtVertex(cols - 1, zn), 1.0f, v1),
                              add(maxX + kSkirtM, z0, terrain.HeightAtVertex(cols - 1, z), 1.0f, v0));
            }
            // Corner patches.
            const float hNW = terrain.HeightAtVertex(0, 0), hNE = terrain.HeightAtVertex(cols - 1, 0);
            const float hSW = terrain.HeightAtVertex(0, rows - 1), hSE = terrain.HeightAtVertex(cols - 1, rows - 1);
            skirt.AddQuad(add(minX - kSkirtM, minZ - kSkirtM, hNW, 0.0f, 0.0f), add(minX - kSkirtM, minZ, hNW, 0.0f, 0.0f),
                          add(minX, minZ, hNW, 0.0f, 0.0f), add(minX, minZ - kSkirtM, hNW, 0.0f, 0.0f));
            skirt.AddQuad(add(maxX, minZ - kSkirtM, hNE, 1.0f, 0.0f), add(maxX, minZ, hNE, 1.0f, 0.0f),
                          add(maxX + kSkirtM, minZ, hNE, 1.0f, 0.0f), add(maxX + kSkirtM, minZ - kSkirtM, hNE, 1.0f, 0.0f));
            skirt.AddQuad(add(minX - kSkirtM, maxZ, hSW, 0.0f, 1.0f), add(minX - kSkirtM, maxZ + kSkirtM, hSW, 0.0f, 1.0f),
                          add(minX, maxZ + kSkirtM, hSW, 0.0f, 1.0f), add(minX, maxZ, hSW, 0.0f, 1.0f));
            skirt.AddQuad(add(maxX, maxZ, hSE, 1.0f, 1.0f), add(maxX, maxZ + kSkirtM, hSE, 1.0f, 1.0f),
                          add(maxX + kSkirtM, maxZ + kSkirtM, hSE, 1.0f, 1.0f), add(maxX + kSkirtM, maxZ, hSE, 1.0f, 1.0f));
            terrainSkirt_ = GpuMesh::Create(device, skirt, VertexLayout::PositionNormalDualTexture);
        }
    }

    void WorldRenderer::BakeRoadColours(MeshData& mesh, const Image& shadow, const Image* tint) const
    {
        const auto& terrain = world_.Terrain();
        const float sizeX = std::max(1.0f, terrain.MaxX() - terrain.MinX());
        const float sizeZ = std::max(1.0f, terrain.MaxZ() - terrain.MinZ());
        for (auto& v : mesh.vertices) {
            const Vector3 irradiance = bakeRig_.Irradiance(v.normal);
            const float sh = GroundShadows::Sample(shadow, world_, v.position.X, v.position.Z);
            const Rgb base{static_cast<float>(v.color.getRProperty()) / 255.0f, static_cast<float>(v.color.getGProperty()) / 255.0f,
                           static_cast<float>(v.color.getBProperty()) / 255.0f};
            Rgb lit{base.r * irradiance.X * sh, base.g * irradiance.Y * sh, base.b * irradiance.Z * sh};
            if (tint) {
                // Verge: blend towards exactly what the terrain shows at this point (the macro
                // colour with its light, canopy shade and ground shadows already applied) so
                // the outer edge disappears into the ground.
                const float a = static_cast<float>(v.color.getAProperty()) / 255.0f;
                const int tx = std::clamp(static_cast<int>((v.position.X - terrain.MinX()) / sizeX * static_cast<float>(tint->Width())), 0, tint->Width() - 1);
                const int tz = std::clamp(static_cast<int>((v.position.Z - terrain.MinZ()) / sizeZ * static_cast<float>(tint->Height())), 0, tint->Height() - 1);
                const Color t = tint->At(tx, tz);
                const Rgb ground{static_cast<float>(t.getRProperty()) / 255.0f, static_cast<float>(t.getGProperty()) / 255.0f,
                                 static_cast<float>(t.getBProperty()) / 255.0f};
                lit = Lerp(lit, ground, a);
            }
            v.color = Color(static_cast<int>(Clamp01(lit.r) * 255.0f), static_cast<int>(Clamp01(lit.g) * 255.0f), static_cast<int>(Clamp01(lit.b) * 255.0f), 255);
        }
    }

    void WorldRenderer::BuildRoads(GraphicsDevice& device, const Image& shadow, const Image& tint)
    {
        RoadMeshBuilder builder(world_.Roads());
        builder.SetTerrainHeight([this](const float x, const float z) { return world_.Terrain().Height(x, z); });
        for (const auto& piece : world_.Roads().Pieces()) {
            RoadPieceMeshes meshes = builder.BuildPiece(piece);
            const auto& road = world_.Roads().Roads()[static_cast<std::size_t>(piece.road)];
            const Surface pavedSurface = road.profile.surface == Sim::SurfaceType::Gravel || road.profile.surface == Sim::SurfaceType::Dirt
                                             ? Surface::Gravel : Surface::Asphalt;
            const auto push = [&](MeshData& m, Surface s, const Image* vergeTint) {
                if (m.TriangleCount() == 0) return;
                Batch b;
                b.source = std::make_shared<MeshData>(m);
                b.verge = vergeTint != nullptr;
                BakeRoadColours(m, shadow, vergeTint);
                b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
                b.surface = s;
                roadBatches_.push_back(std::move(b));
            };
            push(meshes.verge, Surface::Grass, &tint);
            push(meshes.paved, pavedSurface, nullptr);
            if (pavedSurface == Surface::Asphalt && !roadBatches_.empty() && roadBatches_.back().surface == Surface::Asphalt) {
                Batch& asphalt = roadBatches_.back();
                if (meshes.snowBase.TriangleCount() > 0) {
                    asphalt.snowBase = GpuMesh::Create(device, meshes.snowBase, VertexLayout::PositionColorTexture);
                }
                AddPuddleMesh(device, meshes.paved, asphalt);
            }
            push(meshes.shoulder, Surface::Gravel, nullptr);
            push(meshes.sidewalk, Surface::Paving, nullptr);
            push(meshes.kerb, Surface::Concrete, nullptr);
            push(meshes.markings, Surface::Marking, nullptr);
        }
        stats_.roadBatchesTotal = static_cast<int>(roadBatches_.size());
    }

    void WorldRenderer::AddPuddleMesh(GraphicsDevice& device, const MeshData& asphalt, Batch& batch)
    {
        MeshData stretched = asphalt;
        for (auto& v : stretched.vertices) {
            v.uv = Vector2(v.uv.X * 0.25f, v.uv.Y * 0.25f);
        }
        batch.puddles = GpuMesh::Create(device, stretched, VertexLayout::PositionTexture);
    }

    void WorldRenderer::BuildIntersections(GraphicsDevice& device, const Image& shadow)
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
            b.source = std::make_shared<MeshData>(m);
            BakeRoadColours(m, shadow, nullptr);
            b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
            b.surface = s;
            roadBatches_.push_back(std::move(b));
        };
        push(asphalt, Surface::Asphalt);
        if (asphalt.TriangleCount() > 0) {
            AddPuddleMesh(device, asphalt, roadBatches_.back());
        }
        push(gravel, Surface::Gravel);
        push(markings, Surface::Marking);
        stats_.roadBatchesTotal = static_cast<int>(roadBatches_.size());
    }

    void WorldRenderer::BuildPavedAreas(GraphicsDevice& device, const Image& shadow)
    {
        using Microsoft::Xna::Framework::Vector2;
        const auto& terrain = world_.Terrain();
        const auto& ground = world_.Ground();
        constexpr float kCell = 2.0f;        // grid step of the paved surface
        constexpr float kSettTileM = 0.8f;   // one cobble texture tile
        constexpr float kSlabTileM = 1.6f;
        constexpr float kLift = 0.02f;       // above the terrain, under the road surface
        MeshData setts;     // cobbled squares
        MeshData slabs;     // concrete yards and forecourts
        MeshData kerbs;     // concrete edging where the paving meets grass or a road
        for (const auto& region : terrain.Spec().regions) {
            const bool square = region.type == Map::RegionType::Square;
            const bool yard = region.type == Map::RegionType::Yard;
            if ((!square && !yard) || region.polygon.size() < 3) continue;
            MeshData& mesh = square ? setts : slabs;
            const float tile = square ? kSettTileM : kSlabTileM;
            // A four-cornered area (the usual case) is filled with its own bilinear grid, so the
            // paving follows the outline exactly even when it is not axis-aligned; anything else
            // falls back to a grid over the bounding box.
            const bool quad = region.polygon.size() == 4;
            Vector2 c0, c1, c2, c3;
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (const auto& p : region.polygon) {
                minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
                minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
            }
            if (quad) {
                c0 = region.polygon[0]; c1 = region.polygon[1]; c2 = region.polygon[2]; c3 = region.polygon[3];
            }
            const float spanU = quad ? std::max(Vector2::Distance(c0, c1), Vector2::Distance(c3, c2)) : (maxX - minX);
            const float spanV = quad ? std::max(Vector2::Distance(c0, c3), Vector2::Distance(c1, c2)) : (maxZ - minZ);
            const int nu = std::max(1, static_cast<int>(std::ceil(spanU / kCell)));
            const int nv = std::max(1, static_cast<int>(std::ceil(spanV / kCell)));
            const auto point = [&](const float u, const float v) {
                if (!quad) return Vector2(minX + u * (maxX - minX), minZ + v * (maxZ - minZ));
                const Vector2 a = Vector2::Lerp(c0, c1, u);
                const Vector2 b = Vector2::Lerp(c3, c2, u);
                return Vector2::Lerp(a, b, v);
            };
            std::vector<std::uint8_t> kept(static_cast<std::size_t>(nu) * static_cast<std::size_t>(nv), 0u);
            for (int iv = 0; iv < nv; ++iv) {
                for (int iu = 0; iu < nu; ++iu) {
                    const float u0 = static_cast<float>(iu) / static_cast<float>(nu);
                    const float u1 = static_cast<float>(iu + 1) / static_cast<float>(nu);
                    const float v0 = static_cast<float>(iv) / static_cast<float>(nv);
                    const float v1 = static_cast<float>(iv + 1) / static_cast<float>(nv);
                    const Vector2 corners[4] = {point(u0, v0), point(u0, v1), point(u1, v1), point(u1, v0)};
                    // Cells are dropped where the road surface already paves the ground: roads
                    // carry their own surface and sit above the terrain.
                    bool clear = true;
                    for (const Vector2& c : corners) {
                        if (!quad && terrain.RegionAt(c.X, c.Y) != region.type) { clear = false; break; }
                        const Map::SurfaceSample gs = ground.Sample(c.X, c.Y);
                        if (gs.onRoad || gs.distanceToPavedEdge < 0.6f) { clear = false; break; }
                    }
                    if (!clear) continue;
                    // Corner order matches the terrain mesh (counter-clockwise from above), so the
                    // paving is not back-face culled.
                    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
                    for (const Vector2& c : corners) {
                        const Vector3 position(c.X, terrain.Height(c.X, c.Y) + kLift, c.Y);
                        mesh.AddVertex(position, terrain.Normal(c.X, c.Y), Vector2(c.X / tile, c.Y / tile), Color(255, 255, 255, 255));
                    }
                    mesh.AddQuad(base, base + 1, base + 2, base + 3);
                    kept[static_cast<std::size_t>(iv) * static_cast<std::size_t>(nu) + static_cast<std::size_t>(iu)] = 1u;
                }
            }
            // Kerb along the outer boundary of the paving: wherever a kept cell has no kept
            // neighbour, a low concrete band closes the edge against the grass or the road.
            const auto isKept = [&](const int iu, const int iv) {
                if (iu < 0 || iv < 0 || iu >= nu || iv >= nv) return false;
                return kept[static_cast<std::size_t>(iv) * static_cast<std::size_t>(nu) + static_cast<std::size_t>(iu)] != 0u;
            };
            constexpr float kKerbHeight = 0.11f;
            constexpr float kKerbWidth = 0.22f;
            for (int iv = 0; iv < nv; ++iv) {
                for (int iu = 0; iu < nu; ++iu) {
                    if (!isKept(iu, iv)) continue;
                    const float u0 = static_cast<float>(iu) / static_cast<float>(nu);
                    const float u1 = static_cast<float>(iu + 1) / static_cast<float>(nu);
                    const float v0 = static_cast<float>(iv) / static_cast<float>(nv);
                    const float v1 = static_cast<float>(iv + 1) / static_cast<float>(nv);
                    const std::pair<Vector2, Vector2> edges[4] = {
                        {point(u0, v0), point(u0, v1)},   // -u side
                        {point(u1, v1), point(u1, v0)},   // +u side
                        {point(u1, v0), point(u0, v0)},   // -v side
                        {point(u0, v1), point(u1, v1)},   // +v side
                    };
                    const bool open[4] = {!isKept(iu - 1, iv), !isKept(iu + 1, iv), !isKept(iu, iv - 1), !isKept(iu, iv + 1)};
                    for (int e = 0; e < 4; ++e) {
                        if (!open[e]) continue;
                        const Vector2 a = edges[e].first;
                        const Vector2 b = edges[e].second;
                        Vector2 along(b.X - a.X, b.Y - a.Y);
                        const float length = along.Length();
                        if (length < 1e-3f) continue;
                        along = along * (1.0f / length);
                        const Vector2 outward(along.Y, -along.X);   // the kept cell is on the left of a->b
                        const Vector2 a2(a.X + outward.X * kKerbWidth, a.Y + outward.Y * kKerbWidth);
                        const Vector2 b2(b.X + outward.X * kKerbWidth, b.Y + outward.Y * kKerbWidth);
                        const float ya = terrain.Height(a.X, a.Y) + kLift + kKerbHeight;
                        const float yb = terrain.Height(b.X, b.Y) + kLift + kKerbHeight;
                        const std::uint32_t base = static_cast<std::uint32_t>(kerbs.vertices.size());
                        const Vector3 up(0.0f, 1.0f, 0.0f);
                        kerbs.AddVertex(Vector3(a.X, ya, a.Y), up, Vector2(a.X * 0.5f, a.Y * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(b.X, yb, b.Y), up, Vector2(b.X * 0.5f, b.Y * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(b2.X, yb, b2.Y), up, Vector2(b2.X * 0.5f, b2.Y * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(a2.X, ya, a2.Y), up, Vector2(a2.X * 0.5f, a2.Y * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddQuad(base, base + 3, base + 2, base + 1);   // top face, seen from above
                        // Outer face down to the ground.
                        const float ga = terrain.Height(a2.X, a2.Y) + kLift;
                        const float gb = terrain.Height(b2.X, b2.Y) + kLift;
                        const std::uint32_t side = static_cast<std::uint32_t>(kerbs.vertices.size());
                        const Vector3 n(outward.X, 0.0f, outward.Y);
                        kerbs.AddVertex(Vector3(a2.X, ya, a2.Y), n, Vector2(a2.X * 0.5f, ya * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(b2.X, yb, b2.Y), n, Vector2(b2.X * 0.5f, yb * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(b2.X, gb, b2.Y), n, Vector2(b2.X * 0.5f, gb * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddVertex(Vector3(a2.X, ga, a2.Y), n, Vector2(a2.X * 0.5f, ga * 0.5f), Color(255, 255, 255, 255));
                        kerbs.AddQuad(side, side + 1, side + 2, side + 3);
                    }
                }
            }
        }
        const auto push = [&](MeshData& m, const Surface surface) {
            if (m.TriangleCount() == 0) return;
            Batch b;
            b.source = std::make_shared<MeshData>(m);
            BakeRoadColours(m, shadow, nullptr);
            b.mesh = GpuMesh::Create(device, m, VertexLayout::PositionColorTexture);
            b.surface = surface;
            roadBatches_.push_back(std::move(b));
        };
        push(setts, Surface::Cobbles);
        push(slabs, Surface::Paving);
        push(kerbs, Surface::Concrete);
        stats_.roadBatchesTotal = static_cast<int>(roadBatches_.size());
    }

    void WorldRenderer::DrawLampLights(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum)
    {
        if (lampFactor_ <= 0.01f || !glowEffect_ || lampLights_.empty()) {
            return;
        }
        glowEffect_->setWorldProperty(Matrix::getIdentityProperty());
        glowEffect_->setViewProperty(view);
        glowEffect_->setProjectionProperty(projection);
        glowEffect_->setTextureProperty(glowTexture_.get());
        // Sodium-white lamps. The lantern runs near white; the pool on the ground carries its
        // lower intensity in the vertex colour, since both share this one effect.
        glowEffect_->setDiffuseColorProperty(Vector3(0.95f, 0.84f, 0.60f) * lampFactor_);
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& mesh : lampLights_) {
            if (!mesh || !frustum.Intersects(mesh->Sphere())) continue;
            ApplyAll(*glowEffect_, device, *mesh);
            ++stats_.drawCalls;
            stats_.triangles += mesh->PrimitiveCount();
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        glowEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    }

    void WorldRenderer::DrawHeadlightFill(GraphicsDevice& device, const Matrix& view, const Matrix& projection,
                                          const BoundingFrustum& frustum)
    {
        if (headlightIntensity_ <= 0.01f) return;
        const float range = headlightHighBeam_ ? 275.0f : 165.0f;
        const auto inBeam = [&](const BoundingSphere& sphere) {
            const Vector3 delta = sphere.Center - headlightPosition_;
            const float along = Vector3::Dot(delta, headlightForward_);
            if (along + sphere.Radius < 0.0f || along - sphere.Radius > range) return false;
            const float sideways = std::sqrt(std::max(0.0f, delta.X * delta.X + delta.Z * delta.Z - along * along));
            return sideways < sphere.Radius + 5.0f + std::max(0.0f, along) * (headlightHighBeam_ ? 0.15f : 0.22f);
        };
        device.setBlendStateProperty(BlendState::Additive);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;

        auto& object = *headlightObjectEffect_;
        object.setWorldProperty(Matrix::getIdentityProperty());
        object.setViewProperty(view);
        object.setProjectionProperty(projection);
        object.setFogStartProperty(10.0f);
        object.setFogEndProperty(range);
        object.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        auto& light = object.getDirectionalLight0Property();
        light.setDirectionProperty(headlightForward_);
        light.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.72f, 0.70f, 0.63f) :
                                                           Vector3(0.61f, 0.57f, 0.49f)) * headlightIntensity_);
        light.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        for (const auto& batch : objectBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            object.setTextureProperty(batch.texture);
            object.setDiffuseColorProperty(batch.diffuse);
            ApplyAll(object, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }

        auto& trees = *headlightTreeEffect_;
        trees.setWorldProperty(Matrix::getIdentityProperty());
        trees.setViewProperty(view);
        trees.setProjectionProperty(projection);
        trees.setFogStartProperty(6.0f);
        trees.setFogEndProperty(range);
        trees.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.40f, 0.39f, 0.34f) :
                                                           Vector3(0.32f, 0.30f, 0.26f)) * headlightIntensity_);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        for (const auto& batch : treeBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            trees.setTextureProperty(batch.texture);
            ApplyAll(trees, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }
        trees.setDiffuseColorProperty((headlightHighBeam_ ? Vector3(0.59f, 0.56f, 0.48f) :
                                                           Vector3(0.50f, 0.46f, 0.39f)) * headlightIntensity_);
        for (const auto& batch : signBatches_) {
            if (!batch.mesh || !frustum.Intersects(batch.mesh->Sphere()) || !inBeam(batch.mesh->Sphere())) continue;
            trees.setTextureProperty(batch.texture);
            ApplyAll(trees, device, *batch.mesh);
            ++stats_.drawCalls;
            stats_.triangles += batch.mesh->PrimitiveCount();
        }

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
    }

    Texture2D* WorldRenderer::TextureFor(const Surface s) const
    {
        switch (s) {
            case Surface::Asphalt: return asphalt_.get();
            case Surface::Gravel: return gravel_.get();
            case Surface::Paving: return paving_.get();
            case Surface::Concrete: return concrete_.get();
            case Surface::Marking: return marking_.get();
            case Surface::Grass: return grass_.get();
            case Surface::Cobbles: return cobbles_.get();
        }
        return white_.get();
    }

    void WorldRenderer::Draw(GraphicsDevice& device, const Matrix& view, const Matrix& projection, const BoundingFrustum& frustum,
                             const bool mirrored, const float maxDistance)
    {
        const RasterizerState& solid = mirrored ? RasterizerState::CullClockwise : RasterizerState::CullCounterClockwise;
        // Everything this pass may draw stops here.
        // The quality tier scales the main view's distances; a pass with its own cap (the mirror)
        // takes the shorter of the two.
        const float scale = maxDistance > 0.0f ? 1.0f : drawDistanceScale_;
        const float horizon = maxDistance > 0.0f ? std::min(maxDistance, rig_.fogEnd) : rig_.fogEnd * scale;
        const float terrainCull = maxDistance > 0.0f ? std::min(maxDistance, terrainCullDistanceM) : terrainCullDistanceM * scale;
        stats_.terrainChunksDrawn = 0;
        stats_.roadBatchesDrawn = 0;
        stats_.drawCalls = 0;
        stats_.triangles = 0;

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(solid);
        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        device.getSamplerStatesProperty()[1] = SamplerState::LinearClamp;

        // Wet ground, grass and walls read darker: water fills the pores and the fine texture
        // that scattered light back. Applied per frame on top of the rig's scale, since the
        // wetness moves continuously while the lighting refreshes in steps.
        const float wetGround = 1.0f - 0.16f * wetness_;
        terrainEffect_->setDiffuseColorProperty(bakedScale_ * wetGround);
        terrainEffect_->setWorldProperty(Matrix::getIdentityProperty());
        terrainEffect_->setViewProperty(view);
        terrainEffect_->setProjectionProperty(projection);
        const Vector3 eye = Matrix::Invert(view).getTranslationProperty();
        // Lying snow: the same surfaces again, alpha-blended white, pulled forward by the
        // marking depth bias so they win the depth test against themselves.
        const auto beginSnow = [&](const float alpha) {
            snowEffect_->setWorldProperty(Matrix::getIdentityProperty());
            snowEffect_->setViewProperty(view);
            snowEffect_->setProjectionProperty(projection);
            snowEffect_->setTextureProperty(snowTexture_.get());
            // Snow reflects most of the light the baked ground absorbed: well above the grass.
            snowEffect_->setDiffuseColorProperty(Vector3(std::min(1.0f, bakedScale_.X * 1.55f), std::min(1.0f, bakedScale_.Y * 1.58f),
                                                         std::min(1.0f, bakedScale_.Z * 1.66f)));
            snowEffect_->setAlphaProperty(std::clamp(alpha, 0.0f, 1.0f));
            snowEffect_->setFogColorProperty(rig_.fogColor);
            snowEffect_->setFogStartProperty(rig_.fogStart);
            snowEffect_->setFogEndProperty(rig_.fogEnd);
            device.setBlendStateProperty(BlendState::NonPremultiplied);
            device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            device.setRasterizerStateProperty(mirrored ? *markingStateMirrored_ : *markingState_);
            device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;
        };
        const auto endSnow = [&]() {
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setRasterizerStateProperty(solid);
            device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        };
        const bool snowing = snow_ > 0.01f && snowEffect_ && snowTexture_;
        std::vector<const GpuMesh*> snowTerrain;
        if (terrainSkirt_) {
            ApplyAll(*terrainEffect_, device, *terrainSkirt_);
            ++stats_.drawCalls;
            stats_.triangles += terrainSkirt_->PrimitiveCount();
        }
        for (const auto& chunk : terrainChunks_) {
            if (!chunk.lod0) {
                continue;
            }
            const float distance = Vector3::Distance(eye, chunk.centre) - chunk.radius;
            if (distance > terrainCull || !frustum.Intersects(chunk.lod0->Sphere())) {
                continue;
            }
            const GpuMesh* mesh = distance < lod1DistanceM ? chunk.lod0.get() : (distance < lod2DistanceM ? chunk.lod1.get() : chunk.lod2.get());
            if (!mesh) mesh = chunk.lod0.get();
            ApplyAll(*terrainEffect_, device, *mesh);
            ++stats_.terrainChunksDrawn;
            ++stats_.drawCalls;
            stats_.triangles += mesh->PrimitiveCount();
            if (snowing) snowTerrain.push_back(mesh);
        }
        if (snowing) {
            beginSnow(snow_);
            for (const GpuMesh* mesh : snowTerrain) {
                ApplyAll(*snowEffect_, device, *mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        device.getSamplerStatesProperty()[0] = SamplerState::AnisotropicWrap;
        roadUnlitEffect_->setWorldProperty(Matrix::getIdentityProperty());
        roadUnlitEffect_->setViewProperty(view);
        roadUnlitEffect_->setProjectionProperty(projection);
        for (int pass = 0; pass < 2; ++pass) {
            // Pass 0: surfaces; pass 1: markings with a depth bias.
            const bool markings = pass == 1;
            device.setRasterizerStateProperty(markings ? (mirrored ? *markingStateMirrored_ : *markingState_) : solid);
            Vector3 tint = markings ? Vector3(0.92f, 0.92f, 0.90f) : Vector3(1.0f, 1.0f, 1.0f);
            if (wetness_ > 0.0f && !markings) {
                // Per batch below: grass verges darken like the terrain beside them.
            } else if (wetness_ > 0.0f) {
                // Wet asphalt swallows light and takes on the colour of the sky it reflects; the
                // paint on it darkens less, because it stays rough.
                const float wet = wetness_ * (markings ? 0.45f : 1.0f);
                const Vector3 sky = rig_.horizonColor;
                tint = Vector3(tint.X * (1.0f - 0.42f * wet) + sky.X * 0.10f * wet,
                               tint.Y * (1.0f - 0.42f * wet) + sky.Y * 0.10f * wet,
                               tint.Z * (1.0f - 0.40f * wet) + sky.Z * 0.13f * wet);
            }
            roadUnlitEffect_->setDiffuseColorProperty(Vector3(tint.X * bakedScale_.X, tint.Y * bakedScale_.Y, tint.Z * bakedScale_.Z));
            for (const auto& b : roadBatches_) {
                if ((b.surface == Surface::Marking) != markings || !b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                    continue;
                }
                if (wetness_ > 0.0f && !markings) {
                    // Wet asphalt and stone swallow light and take on the colour of the sky
                    // they reflect; grass only darkens, like the terrain beside it.
                    Vector3 t(1.0f, 1.0f, 1.0f);
                    if (b.surface == Surface::Grass) {
                        t = Vector3(wetGround, wetGround, wetGround);
                    } else {
                        const float wet = wetness_ * (b.surface == Surface::Asphalt ? 1.0f : 0.8f);
                        const Vector3 sky = rig_.horizonColor;
                        t = Vector3(1.0f - 0.42f * wet + sky.X * 0.10f * wet, 1.0f - 0.42f * wet + sky.Y * 0.10f * wet,
                                    1.0f - 0.40f * wet + sky.Z * 0.13f * wet);
                    }
                    roadUnlitEffect_->setDiffuseColorProperty(Vector3(t.X * bakedScale_.X, t.Y * bakedScale_.Y, t.Z * bakedScale_.Z));
                }
                roadUnlitEffect_->setTextureProperty(TextureFor(b.surface));
                ApplyAll(*roadUnlitEffect_, device, *b.mesh);
                ++stats_.roadBatchesDrawn;
                ++stats_.drawCalls;
                stats_.triangles += b.mesh->PrimitiveCount();
            }
        }
        // Wet sheen over the road surfaces (not the markings: paint stays rough when it is wet).
        if (wetness_ > 0.01f && roadSheenEffect_) {
            const Vector3 sky = rig_.horizonColor;
            const float strength = 0.95f * wetness_;
            roadSheenEffect_->setWorldProperty(Matrix::getIdentityProperty());
            roadSheenEffect_->setViewProperty(view);
            roadSheenEffect_->setProjectionProperty(projection);
            roadSheenEffect_->setFogColorProperty(Vector3(sky.X * strength, sky.Y * strength, sky.Z * strength));
            // The ramp: nothing under the bumper, full sheen by the time the road is a hundred
            // metres off, and never beyond the fog, where there is no road left to see.
            roadSheenEffect_->setFogStartProperty(7.0f);
            roadSheenEffect_->setFogEndProperty(std::min(150.0f, rig_.fogEnd));
            // The road loop left the marking rasterizer state (with its depth bias) in place;
            // the sheen is the same geometry as the surfaces, so it wants the plain one.
            device.setRasterizerStateProperty(solid);
            device.setBlendStateProperty(BlendState::Additive);
            device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            for (const auto& b : roadBatches_) {
                if (b.surface == Surface::Marking || b.surface == Surface::Gravel || b.surface == Surface::Grass || !b.mesh ||
                    !frustum.Intersects(b.mesh->Sphere())) {
                    continue;
                }
                // Smooth asphalt mirrors the most; slabs, kerbs and cobbles are rougher and
                // drain between their joints.
                const float surfaceShare = b.surface == Surface::Asphalt ? 1.0f : (b.surface == Surface::Cobbles ? 0.55f : 0.75f);
                roadSheenEffect_->setFogColorProperty(sky * (strength * surfaceShare));
                ApplyAll(*roadSheenEffect_, device, *b.mesh);
                ++stats_.drawCalls;
                stats_.triangles += b.mesh->PrimitiveCount();
            }
            // Standing water once the rain has soaked the road: brighter patches of reflected
            // sky, fading out with distance like the sheen.
            const float standing = std::clamp((wetness_ - 0.35f) / 0.65f, 0.0f, 1.0f);
            if (standing > 0.0f && puddleEffect_ && puddleMask_) {
                puddleEffect_->setWorldProperty(Matrix::getIdentityProperty());
                puddleEffect_->setViewProperty(view);
                puddleEffect_->setProjectionProperty(projection);
                puddleEffect_->setTextureProperty(puddleMask_.get());
                puddleEffect_->setDiffuseColorProperty(Vector3(0.015f, 0.015f, 0.02f) + sky * (0.20f * standing));
                puddleEffect_->setFogStartProperty(6.0f);
                puddleEffect_->setFogEndProperty(std::min(120.0f, rig_.fogEnd));
                // The puddles lie on the road surface itself; the marking state's depth bias
                // keeps them from fighting it for the depth test.
                device.setRasterizerStateProperty(mirrored ? *markingStateMirrored_ : *markingState_);
                for (const auto& b : roadBatches_) {
                    if (!b.puddles || !frustum.Intersects(b.puddles->Sphere())) {
                        continue;
                    }
                    ApplyAll(*puddleEffect_, device, *b.puddles);
                    ++stats_.drawCalls;
                    stats_.triangles += b.puddles->PrimitiveCount();
                }
                device.setRasterizerStateProperty(solid);
            }
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
        }

        // Snow on the roads and pavements: a little thinner than on the fields, where the
        // traffic has worn it into slush.
        if (snowing) {
            beginSnow(snow_ * 0.78f);
            for (const auto& b : roadBatches_) {
                if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) continue;
                ApplyAll(*snowEffect_, device, b.snowBase ? *b.snowBase : *b.mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        // Static objects: buildings, props, trunks (lit, textured).
        device.setRasterizerStateProperty(solid);
        roadEffect_->setWorldProperty(Matrix::getIdentityProperty());
        roadEffect_->setViewProperty(view);
        roadEffect_->setProjectionProperty(projection);
        stats_.objectBatchesDrawn = 0;
        for (const auto& b : objectBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            // Nothing past the fog end can be told from the fog itself, so no batch is drawn
            // beyond it; detail batches keep their own shorter range. Without this the buildings
            // of a settlement three kilometres away are still submitted in full.
            const float cull = b.cullDistance > 0.0f ? std::min(b.cullDistance, horizon) : horizon;
            if (Vector3::Distance(eye, b.mesh->Sphere().Center) - b.mesh->Sphere().Radius > cull) {
                continue;
            }
            roadEffect_->setTextureProperty(b.texture);
            // Rain-soaked plaster, roof tiles and wood darken and pick up a wet gloss.
            roadEffect_->setDiffuseColorProperty(b.diffuse * (1.0f - 0.22f * wetness_));
            roadEffect_->setEmissiveColorProperty(b.emissive + b.nightEmissive * lampFactor_);
            roadEffect_->setSpecularColorProperty(b.specular + Vector3(0.20f, 0.20f, 0.21f) * wetness_);
            roadEffect_->setSpecularPowerProperty(b.specularPower + 20.0f * wetness_);
            ApplyAll(*roadEffect_, device, *b.mesh);
            ++stats_.objectBatchesDrawn;
            ++stats_.drawCalls;
            stats_.triangles += b.mesh->PrimitiveCount();
        }
        roadEffect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        roadEffect_->setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        roadEffect_->setSpecularColorProperty(Vector3(0.06f, 0.06f, 0.06f));
        roadEffect_->setSpecularPowerProperty(10.0f);
        // Snow on the roofs.
        if (snowing) {
            beginSnow(snow_);
            for (const auto& b : objectBatches_) {
                if (!b.roof || !b.mesh || !frustum.Intersects(b.mesh->Sphere())) continue;
                if (Vector3::Distance(eye, b.mesh->Sphere().Center) - b.mesh->Sphere().Radius > horizon) continue;
                ApplyAll(*snowEffect_, device, *b.mesh);
                ++stats_.drawCalls;
            }
            endSnow();
        }

        // Trees: alpha-tested cards, both windings present, distance culled.
        const Vector3 cameraPosition = Matrix::Invert(view).getTranslationProperty();
        // Alpha-tested foliage otherwise leaves pale cut-out silhouettes after the rest of the
        // world has vanished into dense fog. Use the same visibility horizon as opaque objects;
        // the effect's fog ramp hides the last visible cards before a batch is culled.
        const float treeRange = std::min(horizon, maxDistance > 0.0f ? std::min(maxDistance, 1100.0f) : 1100.0f * scale * vegetationScale_);
        device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        treeEffect_->setDiffuseColorProperty(bakedScale_ * (1.0f - 0.10f * wetness_));
        treeEffect_->setWorldProperty(Matrix::getIdentityProperty());
        treeEffect_->setViewProperty(view);
        treeEffect_->setProjectionProperty(projection);
        stats_.treeBatchesDrawn = 0;
        for (const auto& b : treeBatches_) {
            if (!b.mesh || !frustum.Intersects(b.mesh->Sphere())) {
                continue;
            }
            const BoundingSphere& sphere = b.mesh->Sphere();
            const float centreDistance = Vector3::Distance(sphere.Center, cameraPosition);
            if (centreDistance - sphere.Radius > treeRange) {
                continue;
            }
            if (rig_.fogEnd < 400.0f && centreDistance > treeRange + 30.0f) {
                continue;   // a coarse 256 m chunk must not leave a white silhouette beyond dense fog
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

        if (!mirrored) DrawHeadlightFill(device, view, projection, frustum);
        // Street lamp light last, so it adds on top of everything the pass has drawn.
        DrawLampLights(device, view, projection, frustum);
    }
}
