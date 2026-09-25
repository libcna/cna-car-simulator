// Static terrain, road and paved-area mesh construction for WorldRenderer. These are the
// existing algorithms, kept as member methods so the culling and render owners stay shared.
#include "CarSim/Render/WorldRenderer.hpp"

#include "CarSim/Render/GroundShadowBaker.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Render/Image.hpp"
#include "CarSim/Render/RoadMeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace CarSim::Render
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        constexpr int kChunkCells = 32;
        constexpr float kGrassTileM = 7.0f;
        float Clamp01(const float v) { return std::clamp(v, 0.0f, 1.0f); }
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
        // The region classification is categorical; retain a small mask so the forest/meadow
        // boundary can be feathered after the normal lighting and shadow bake.
        std::vector<std::uint8_t> forestEdgeMask(static_cast<std::size_t>(width) * height, 0);
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
                forestEdgeMask[static_cast<std::size_t>(y) * width + x] = region == Map::RegionType::Forest ? 1u :
                                                                         region == Map::RegionType::Meadow ? 2u : 0u;
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
                    case Map::RegionType::Forest: {
                        // Needle litter, darker soil and moss occupy patches larger than the
                        // grass detail tile. Keep them subdued under the canopy, but no longer
                        // paint the entire forest floor the same brown.
                        const float moss = Clamp01(0.50f + 0.55f * patch + 0.18f * variation);
                        const float litter = Clamp01(0.28f + 0.32f * dryness - 0.20f * patch);
                        tint = Lerp(Rgb{0.31f, 0.28f, 0.22f}, Rgb{0.43f, 0.44f, 0.27f}, moss);
                        tint = Lerp(tint, Rgb{0.45f, 0.34f, 0.24f}, litter * 0.35f);
                        light *= 0.72f;   // canopy shade
                        break;
                    }
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
        // Feather only forest/meadow edges, over roughly 6 m. Read the untouched macro from
        // tintOut, which already exists for road-verge baking, so shadows and neighbouring
        // colour patches remain consistent without another full-size image allocation.
        for (int y = 2; y + 2 < height; ++y) {
            for (int x = 2; x + 2 < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const auto kind = forestEdgeMask[index];
                if (kind == 0u) continue;
                bool boundary = false;
                for (const auto& [dx, dy] : {std::pair{-2, 0}, std::pair{2, 0}, std::pair{0, -2}, std::pair{0, 2}}) {
                    const auto other = forestEdgeMask[static_cast<std::size_t>(y + dy) * width + x + dx];
                    boundary |= other != 0u && other != kind;
                }
                if (!boundary) continue;
                Rgb sum{};
                float count = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (forestEdgeMask[static_cast<std::size_t>(y + dy) * width + x + dx] == 0u) continue;
                        const Color& c = tintOut.At(x + dx, y + dy);
                        sum = sum + Rgb::FromBytes(c.getRProperty(), c.getGProperty(), c.getBProperty());
                        count += 1.0f;
                    }
                }
                const Color& base = tintOut.At(x, y);
                const Rgb original = Rgb::FromBytes(base.getRProperty(), base.getGProperty(), base.getBProperty());
                macro.Set(x, y, Lerp(original, sum * (1.0f / count), 0.7f));
            }
        }
        tintOut = macro;
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
            // The normal terrain pass uses two UV sets. Its snow overlay is unlit and needs
            // only position + the first UV; giving BasicEffect that compact declaration also
            // keeps the draw valid on every CNA renderer without a renderer-specific path.
            auto ground = GpuMesh::Create(device, mesh, VertexLayout::PositionNormalDualTexture);
            auto snow = GpuMesh::Create(device, mesh, VertexLayout::PositionTexture, ground.get());
            return std::pair{std::move(ground), std::move(snow)};
        };
        for (int cz = 0; cz + 1 < rows; cz += kChunkCells) {
            for (int cx = 0; cx + 1 < cols; cx += kChunkCells) {
                const int x1 = std::min(cols - 1, cx + kChunkCells);
                const int z1 = std::min(rows - 1, cz + kChunkCells);
                TerrainChunk chunk;
                auto [lod0, snowLod0] = build(cx, cz, x1, z1, 1);
                auto [lod1, snowLod1] = build(cx, cz, x1, z1, 2);
                auto [lod2, snowLod2] = build(cx, cz, x1, z1, 4);
                chunk.lod0 = std::move(lod0);
                chunk.lod1 = std::move(lod1);
                chunk.lod2 = std::move(lod2);
                chunk.snowLod0 = std::move(snowLod0);
                chunk.snowLod1 = std::move(snowLod1);
                chunk.snowLod2 = std::move(snowLod2);
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

}
