#include "CarSim/Render/GroundShadowBaker.hpp"

#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Map/TerrainField.hpp"
#include "CarSim/Render/ShadowGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CarSim::Render::GroundShadows
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    Vector3 ShadowOffset(const Vector3& sunDirection, const float height)
    {
        const float horizontal = std::hypot(sunDirection.X, sunDirection.Z);
        if (horizontal < 1e-5f || sunDirection.Y >= -1e-4f || height <= 0.0f) {
            return Vector3(0.0f, 0.0f, 0.0f);
        }
        const float length = height * horizontal / -sunDirection.Y;   // height / tan(elevation)
        return Vector3(sunDirection.X / horizontal * length, 0.0f, sunDirection.Z / horizontal * length);
    }

    namespace
    {
        struct Grid
        {
            std::vector<float> factor;   // 1 = lit
            int width = 0, height = 0;
            float minX = 0, minZ = 0, sizeX = 1, sizeZ = 1;

            [[nodiscard]] float TexelX(const float x) const { return (x - minX) / sizeX * static_cast<float>(width) - 0.5f; }
            [[nodiscard]] float TexelZ(const float z) const { return (z - minZ) / sizeZ * static_cast<float>(height) - 0.5f; }
            [[nodiscard]] float WorldX(const int tx) const { return minX + (static_cast<float>(tx) + 0.5f) / static_cast<float>(width) * sizeX; }
            [[nodiscard]] float WorldZ(const int tz) const { return minZ + (static_cast<float>(tz) + 0.5f) / static_cast<float>(height) * sizeZ; }

            void Darken(const int tx, const int tz, const float f)
            {
                if (tx < 0 || tz < 0 || tx >= width || tz >= height) return;
                float& v = factor[static_cast<std::size_t>(tz * width + tx)];
                v = std::min(v, f);
            }

            /// Fills a convex counter-clockwise polygon (world x/z) with the factor.
            void FillConvex(const std::vector<Vector2>& poly, const float f)
            {
                if (poly.size() < 3) return;
                float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
                for (const auto& p : poly) { x0 = std::min(x0, p.X); x1 = std::max(x1, p.X); z0 = std::min(z0, p.Y); z1 = std::max(z1, p.Y); }
                const int tx0 = std::max(0, static_cast<int>(std::floor(TexelX(x0))));
                const int tx1 = std::min(width - 1, static_cast<int>(std::ceil(TexelX(x1))));
                const int tz0 = std::max(0, static_cast<int>(std::floor(TexelZ(z0))));
                const int tz1 = std::min(height - 1, static_cast<int>(std::ceil(TexelZ(z1))));
                for (int tz = tz0; tz <= tz1; ++tz) {
                    const float wz = WorldZ(tz);
                    for (int tx = tx0; tx <= tx1; ++tx) {
                        const float wx = WorldX(tx);
                        bool inside = true;
                        for (std::size_t i = 0; i < poly.size() && inside; ++i) {
                            const Vector2& a = poly[i];
                            const Vector2& b = poly[(i + 1) % poly.size()];
                            const float cross = (b.X - a.X) * (wz - a.Y) - (b.Y - a.Y) * (wx - a.X);
                            if (cross < 0.0f) inside = false;
                        }
                        if (inside) Darken(tx, tz, f);
                    }
                }
            }

            void FillDisc(const float cx, const float cz, const float radius, const float f)
            {
                const int tx0 = std::max(0, static_cast<int>(std::floor(TexelX(cx - radius))));
                const int tx1 = std::min(width - 1, static_cast<int>(std::ceil(TexelX(cx + radius))));
                const int tz0 = std::max(0, static_cast<int>(std::floor(TexelZ(cz - radius))));
                const int tz1 = std::min(height - 1, static_cast<int>(std::ceil(TexelZ(cz + radius))));
                const float r2 = radius * radius;
                for (int tz = tz0; tz <= tz1; ++tz) {
                    const float dz = WorldZ(tz) - cz;
                    for (int tx = tx0; tx <= tx1; ++tx) {
                        const float dx = WorldX(tx) - cx;
                        if (dx * dx + dz * dz <= r2) Darken(tx, tz, f);
                    }
                }
            }
        };
    }

    Image Bake(const Map::MapWorld& world, const Vector3& sunDirection, const int width, const int height)
    {
        const auto& terrain = world.Terrain();
        Grid grid;
        grid.width = std::max(1, width);
        grid.height = std::max(1, height);
        grid.minX = terrain.MinX();
        grid.minZ = terrain.MinZ();
        grid.sizeX = std::max(1.0f, terrain.MaxX() - terrain.MinX());
        grid.sizeZ = std::max(1.0f, terrain.MaxZ() - terrain.MinZ());
        grid.factor.assign(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), 1.0f);

        // Buildings: the shadow of a box is the convex hull of its footprint and of the footprint
        // shifted by the offset of its (average) roof height.
        for (const auto& b : world.Objects().Buildings()) {
            const float h = b.height + 0.5f * b.roofHeight;
            const Vector3 offset = ShadowOffset(sunDirection, h);
            const Vector3 facing(std::sin(b.headingRad), 0.0f, -std::cos(b.headingRad));
            const Vector3 along(std::cos(b.headingRad), 0.0f, std::sin(b.headingRad));
            std::vector<Vector2> pts;
            for (const float sw : {-1.0f, 1.0f}) {
                for (const float sd : {-1.0f, 1.0f}) {
                    const Vector3 c = b.position + along * (sw * b.halfWidth) + facing * (sd * b.halfDepth);
                    pts.emplace_back(c.X, c.Z);
                    pts.emplace_back(c.X + offset.X, c.Z + offset.Z);
                }
            }
            const std::vector<Vector2> hull = ShadowGeometry::ConvexHull(pts);
            if (hull.size() >= 3) {
                // ConvexHull is counter-clockwise in (x, z); FillConvex expects the same orientation.
                grid.FillConvex(hull, 0.45f);
            }
        }
        // Trees: the crown as a disc offset by its centre height.
        for (const auto& t : world.Objects().Trees()) {
            const float crown = t.CrownRadius();
            const float centreHeight = std::max(0.5f, t.Height() - crown);
            const Vector3 offset = ShadowOffset(sunDirection, centreHeight);
            grid.FillDisc(t.position.X + offset.X, t.position.Z + offset.Z, crown, 0.52f);
        }

        // One box blur pass: a texel of penumbra, and no hard stair-steps on the terrain.
        std::vector<float> blurred(grid.factor.size());
        for (int tz = 0; tz < grid.height; ++tz) {
            for (int tx = 0; tx < grid.width; ++tx) {
                float sum = 0.0f;
                int n = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = tx + dx, z = tz + dz;
                        if (x < 0 || z < 0 || x >= grid.width || z >= grid.height) continue;
                        sum += grid.factor[static_cast<std::size_t>(z * grid.width + x)];
                        ++n;
                    }
                }
                blurred[static_cast<std::size_t>(tz * grid.width + tx)] = n > 0 ? sum / static_cast<float>(n) : 1.0f;
            }
        }
        Image image(grid.width, grid.height);
        for (int tz = 0; tz < grid.height; ++tz) {
            for (int tx = 0; tx < grid.width; ++tx) {
                const int v = static_cast<int>(std::clamp(blurred[static_cast<std::size_t>(tz * grid.width + tx)], 0.0f, 1.0f) * 255.0f + 0.5f);
                image.FillRect(tx, tz, tx + 1, tz + 1, Color(v, v, v, 255));
            }
        }
        return image;
    }

    float Sample(const Image& map, const Map::MapWorld& world, const float x, const float z)
    {
        const auto& terrain = world.Terrain();
        const float sizeX = std::max(1.0f, terrain.MaxX() - terrain.MinX());
        const float sizeZ = std::max(1.0f, terrain.MaxZ() - terrain.MinZ());
        const float fx = std::clamp((x - terrain.MinX()) / sizeX * static_cast<float>(map.Width()) - 0.5f, 0.0f, static_cast<float>(map.Width() - 1));
        const float fz = std::clamp((z - terrain.MinZ()) / sizeZ * static_cast<float>(map.Height()) - 0.5f, 0.0f, static_cast<float>(map.Height() - 1));
        const int x0 = static_cast<int>(fx), z0 = static_cast<int>(fz);
        const int x1 = std::min(x0 + 1, map.Width() - 1), z1 = std::min(z0 + 1, map.Height() - 1);
        const float tx = fx - static_cast<float>(x0), tz = fz - static_cast<float>(z0);
        const auto at = [&](const int px, const int pz) { return static_cast<float>(map.At(px, pz).getRProperty()) / 255.0f; };
        const float top = at(x0, z0) * (1.0f - tx) + at(x1, z0) * tx;
        const float bottom = at(x0, z1) * (1.0f - tx) + at(x1, z1) * tx;
        return top * (1.0f - tz) + bottom * tz;
    }
}
