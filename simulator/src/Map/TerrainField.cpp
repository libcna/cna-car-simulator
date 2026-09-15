#include "CarSim/Map/TerrainField.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Map/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CarSim::Map
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        float SmoothStep(const float t) { const float c = std::clamp(t, 0.0f, 1.0f); return c * c * (3.0f - 2.0f * c); }
        constexpr float kRoadSink = 0.12f;   // terrain sits this far below the road surface (depth precision margin)
    }

    void TerrainField::Build(const TerrainSpec& spec)
    {
        spec_ = spec;
        spec_.cellSize = std::clamp(spec.cellSize, 1.0f, 10.0f);
        columns_ = std::max(2, static_cast<int>(std::round(spec_.sizeX / spec_.cellSize)) + 1);
        rows_ = std::max(2, static_cast<int>(std::round(spec_.sizeZ / spec_.cellSize)) + 1);
        heights_.assign(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_), spec_.baseHeight);
        roadDistance_.assign(heights_.size(), 1e6f);
        minHeight_ = std::numeric_limits<float>::max();
        maxHeight_ = -minHeight_;
        for (int row = 0; row < rows_; ++row) {
            const float z = MinZ() + static_cast<float>(row) * spec_.cellSize;
            for (int col = 0; col < columns_; ++col) {
                const float x = MinX() + static_cast<float>(col) * spec_.cellSize;
                const float h = RawHeight(x, z);
                heights_[Index(col, row)] = h;
                minHeight_ = std::min(minHeight_, h);
                maxHeight_ = std::max(maxHeight_, h);
            }
        }
        ClassifyRegions();
    }

    float TerrainField::RawHeight(const float x, const float z) const
    {
        float h = spec_.baseHeight;
        if (spec_.noiseAmplitude > 0.0f && spec_.noiseWavelength > 1.0f) {
            const float inv = 1.0f / spec_.noiseWavelength;
            h += spec_.noiseAmplitude * Core::Noise::FbmSigned(x * inv + 100.0f, z * inv + 100.0f, std::max(1, spec_.noiseOctaves), 0.5f, spec_.seed);
        }
        const Vector2 p(x, z);
        for (const auto& f : spec_.features) {
            const float r = std::max(1.0f, f.radius);
            float d = 0.0f;
            if (f.type == TerrainFeatureType::Ridge) {
                float t = 0.0f;
                d = DistanceToSegment(p, f.center, f.end, t);
            } else {
                d = Vector2::Distance(p, f.center);
            }
            const float q = d / r;
            switch (f.type) {
                case TerrainFeatureType::Hill:
                case TerrainFeatureType::Ridge:
                    h += f.height * std::exp(-2.0f * q * q);
                    break;
                case TerrainFeatureType::Plateau:
                    h += f.height * (1.0f - SmoothStep((q - 0.6f) / 0.4f));
                    break;
            }
        }
        return h;
    }

    void TerrainField::ConformToRoads(const RoadNetwork& network)
    {
        const float blend = std::max(1.0f, spec_.roadBlendWidth);
        float maxHalf = 0.0f;
        for (const auto& road : network.Roads()) {
            maxHalf = std::max(maxHalf, road.profile.HalfTotalWidth());
        }
        const float reach = maxHalf + blend + 1.0f;
        const auto& intersections = network.Intersections();
        for (int row = 0; row < rows_; ++row) {
            const float z = MinZ() + static_cast<float>(row) * spec_.cellSize;
            for (int col = 0; col < columns_; ++col) {
                const float x = MinX() + static_cast<float>(col) * spec_.cellSize;
                const Vector2 p(x, z);
                float bestD = std::numeric_limits<float>::max();   // distance outside the paved+shoulder edge (<= 0 inside)
                float targetHeight = 0.0f;
                RoadHit hit;
                if (network.NearestRoad(p, reach, hit)) {
                    const auto& road = network.Roads()[static_cast<std::size_t>(hit.road)];
                    const float ht = road.profile.HalfTotalWidth();
                    const float lat = std::fabs(hit.lateral);
                    RoadHit edge = hit;
                    edge.lateral = std::clamp(hit.lateral, -ht, ht);
                    targetHeight = network.SurfaceHeightAt(edge, p) - kRoadSink;
                    bestD = lat - ht;
                }
                // Intersection patches: flat at the node height.
                for (const auto& inter : intersections) {
                    const float dc = Vector2::Distance(p, Vector2(inter.center.X, inter.center.Z));
                    if (dc > inter.radius + blend + 1.0f) continue;
                    float edgeDist = std::numeric_limits<float>::max();
                    for (std::size_t i = 0, j = inter.patch.size() - 1; i < inter.patch.size(); j = i++) {
                        float t = 0.0f;
                        edgeDist = std::min(edgeDist, DistanceToSegment(p, inter.patch[j], inter.patch[i], t));
                    }
                    const float d = PointInPolygon(p, inter.patch) ? -edgeDist : edgeDist;
                    if (d < bestD) {
                        bestD = d;
                        targetHeight = inter.PlaneHeight(p) - kRoadSink;
                    }
                }
                if (bestD == std::numeric_limits<float>::max()) {
                    continue;
                }
                const std::size_t idx = Index(col, row);
                roadDistance_[idx] = bestD;
                if (bestD <= 0.3f) {
                    heights_[idx] = targetHeight;
                } else if (bestD < blend) {
                    const float t = SmoothStep((bestD - 0.3f) / (blend - 0.3f));
                    heights_[idx] = targetHeight + (heights_[idx] - targetHeight) * t;
                }
            }
        }
        minHeight_ = std::numeric_limits<float>::max();
        maxHeight_ = -minHeight_;
        for (const float h : heights_) {
            minHeight_ = std::min(minHeight_, h);
            maxHeight_ = std::max(maxHeight_, h);
        }
    }

    bool TerrainField::Contains(const float x, const float z) const
    {
        return x >= MinX() && x <= MaxX() && z >= MinZ() && z <= MaxZ();
    }

    float TerrainField::Height(const float x, const float z) const
    {
        const float fx = std::clamp((x - MinX()) / spec_.cellSize, 0.0f, static_cast<float>(columns_ - 1) - 1e-4f);
        const float fz = std::clamp((z - MinZ()) / spec_.cellSize, 0.0f, static_cast<float>(rows_ - 1) - 1e-4f);
        const int c0 = static_cast<int>(fx);
        const int r0 = static_cast<int>(fz);
        const int c1 = std::min(c0 + 1, columns_ - 1);
        const int r1 = std::min(r0 + 1, rows_ - 1);
        const float tx = fx - static_cast<float>(c0);
        const float tz = fz - static_cast<float>(r0);
        const float h00 = heights_[Index(c0, r0)];
        const float h10 = heights_[Index(c1, r0)];
        const float h01 = heights_[Index(c0, r1)];
        const float h11 = heights_[Index(c1, r1)];
        // Match the mesh triangulation (diagonal from (0,0) to (1,1)).
        if (tx >= tz) {
            return h00 + (h10 - h00) * tx + (h11 - h10) * tz;
        }
        return h00 + (h11 - h01) * tx + (h01 - h00) * tz;
    }

    Vector3 TerrainField::Normal(const float x, const float z) const
    {
        const float d = spec_.cellSize * 0.5f;
        const float hx = Height(x + d, z) - Height(x - d, z);
        const float hz = Height(x, z + d) - Height(x, z - d);
        Vector3 n(-hx, 2.0f * d, -hz);
        n.Normalize();
        return n;
    }

    void TerrainField::ClassifyRegions()
    {
        region_.assign(heights_.size(), static_cast<std::uint8_t>(RegionType::Meadow));
        regionIndex_.assign(heights_.size(), -1);
        for (std::size_t ri = 0; ri < spec_.regions.size(); ++ri) {
            const auto& region = spec_.regions[ri];
            if (region.polygon.size() < 3) continue;
            float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
            for (const auto& p : region.polygon) {
                minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
                minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
            }
            const int c0 = std::clamp(static_cast<int>((minX - MinX()) / spec_.cellSize), 0, columns_ - 1);
            const int c1 = std::clamp(static_cast<int>((maxX - MinX()) / spec_.cellSize) + 1, 0, columns_ - 1);
            const int r0 = std::clamp(static_cast<int>((minZ - MinZ()) / spec_.cellSize), 0, rows_ - 1);
            const int r1 = std::clamp(static_cast<int>((maxZ - MinZ()) / spec_.cellSize) + 1, 0, rows_ - 1);
            for (int row = r0; row <= r1; ++row) {
                for (int col = c0; col <= c1; ++col) {
                    const Vector2 p(MinX() + static_cast<float>(col) * spec_.cellSize, MinZ() + static_cast<float>(row) * spec_.cellSize);
                    if (PointInPolygon(p, region.polygon)) {
                        region_[Index(col, row)] = static_cast<std::uint8_t>(region.type);
                        regionIndex_[Index(col, row)] = static_cast<std::int16_t>(ri);
                    }
                }
            }
        }
    }

    RegionType TerrainField::RegionAt(const float x, const float z) const
    {
        const int col = std::clamp(static_cast<int>(std::round((x - MinX()) / spec_.cellSize)), 0, columns_ - 1);
        const int row = std::clamp(static_cast<int>(std::round((z - MinZ()) / spec_.cellSize)), 0, rows_ - 1);
        return static_cast<RegionType>(region_[Index(col, row)]);
    }

    const RegionSpec* TerrainField::RegionSpecAt(const float x, const float z) const
    {
        const int col = std::clamp(static_cast<int>(std::round((x - MinX()) / spec_.cellSize)), 0, columns_ - 1);
        const int row = std::clamp(static_cast<int>(std::round((z - MinZ()) / spec_.cellSize)), 0, rows_ - 1);
        const std::int16_t idx = regionIndex_[Index(col, row)];
        return idx >= 0 ? &spec_.regions[static_cast<std::size_t>(idx)] : nullptr;
    }
}
