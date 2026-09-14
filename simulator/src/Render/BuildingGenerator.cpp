#include "CarSim/Render/BuildingGenerator.hpp"

#include "CarSim/Core/Noise.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        const Color kWhite(255, 255, 255, 255);

        struct Frame
        {
            Matrix toWorld;
            void Append(MeshData& dst, MeshData& local) const { dst.Append(local, toWorld); }
        };

        /// Axis-aligned box in local space with per-face UVs in metres.
        void Box(MeshData& m, const Vector3& min, const Vector3& max, const float uvScale = 0.5f)
        {
            m.AddBox(min, max, uvScale);
        }

        /// Quad facing +n at the given corners (counter-clockwise from the front).
        void Quad(MeshData& m, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& n,
                  const float u1 = 1.0f, const float v1 = 1.0f)
        {
            m.AddQuad(a, b, c, d, n, Vector2(0, v1), Vector2(u1, v1), Vector2(u1, 0), Vector2(0, 0), kWhite);
        }

        /// Gable or hipped roof over a rectangle x in [-hw, hw], z in [-hd, hd] at eaves height `y0`.
        void Roof(MeshData& roof, MeshData& walls, const float hw, const float hd, const float y0, const float ridge,
                  const bool hipped, const float overhang, const float thickness)
        {
            const float ox = hw + overhang;
            const float oz = hd + overhang;
            const float yEave = y0 - overhang * (ridge / hd) * 0.0f;   // eaves stay at y0
            const float hipInset = hipped ? std::min(hw * 0.6f, hd) : 0.0f;
            const Vector3 rL(-hw + hipInset, y0 + ridge, 0.0f);
            const Vector3 rR(hw - hipInset, y0 + ridge, 0.0f);
            // Extend the ridge for the overhang on gable roofs.
            const Vector3 ridgeL = hipped ? rL : Vector3(-ox, y0 + ridge, 0.0f);
            const Vector3 ridgeR = hipped ? rR : Vector3(ox, y0 + ridge, 0.0f);
            const float uvAlong = (ox * 2.0f) / 1.0f;
            const float slopeLen = std::sqrt(oz * oz + ridge * ridge);
            // Front slope (+z) and back slope (-z), seen from outside.
            for (const float side : {1.0f, -1.0f}) {
                const Vector3 eL(-ox, yEave, side * oz);
                const Vector3 eR(ox, yEave, side * oz);
                Vector3 n(0.0f, oz, side * ridge);
                n.Normalize();
                if (side > 0.0f) {
                    roof.AddQuad(eL, eR, ridgeR, ridgeL, n, Vector2(0, slopeLen), Vector2(uvAlong, slopeLen), Vector2(uvAlong, 0), Vector2(0, 0), kWhite);
                } else {
                    roof.AddQuad(eR, eL, ridgeL, ridgeR, n, Vector2(0, slopeLen), Vector2(uvAlong, slopeLen), Vector2(uvAlong, 0), Vector2(0, 0), kWhite);
                }
                // Underside/thickness: a thin slab below the slope so the eaves have depth.
                const Vector3 down(0.0f, -thickness, 0.0f);
                if (side > 0.0f) {
                    roof.AddQuad(eR + down, eL + down, ridgeL + down, ridgeR + down, n * -1.0f, Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), kWhite);
                    roof.AddQuad(eL + down, eR + down, eR, eL, Vector3(0, 0, 1), Vector2(0, 0.1f), Vector2(uvAlong, 0.1f), Vector2(uvAlong, 0), Vector2(0, 0), kWhite);
                } else {
                    roof.AddQuad(eL + down, eR + down, ridgeR + down, ridgeL + down, n * -1.0f, Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), kWhite);
                    roof.AddQuad(eR + down, eL + down, eL, eR, Vector3(0, 0, -1), Vector2(0, 0.1f), Vector2(uvAlong, 0.1f), Vector2(uvAlong, 0), Vector2(0, 0), kWhite);
                }
            }
            if (hipped) {
                // End slopes.
                for (const float side : {1.0f, -1.0f}) {
                    const Vector3 eF(side * ox, yEave, oz);
                    const Vector3 eB(side * ox, yEave, -oz);
                    const Vector3 r = side > 0.0f ? rR : rL;
                    Vector3 n(side * ridge, hipInset > 0.0f ? ox - hw + hipInset : 1.0f, 0.0f);
                    n = Vector3(side * ridge, hipInset, 0.0f);
                    n.Normalize();
                    const std::uint32_t i0 = roof.AddVertex(eF, n, Vector2(0, slopeLen), kWhite);
                    const std::uint32_t i1 = roof.AddVertex(eB, n, Vector2(oz * 2.0f, slopeLen), kWhite);
                    const std::uint32_t i2 = roof.AddVertex(r, n, Vector2(oz, 0), kWhite);
                    if (side > 0.0f) roof.AddTriangle(i0, i1, i2); else roof.AddTriangle(i1, i0, i2);
                }
            } else {
                // Gable end walls (triangles) in the wall material, flush with the walls.
                for (const float side : {1.0f, -1.0f}) {
                    const Vector3 a(side * hw, y0, side * hd);
                    const Vector3 b(side * hw, y0, -side * hd);
                    const Vector3 t(side * hw, y0 + ridge, 0.0f);
                    const Vector3 n(side, 0.0f, 0.0f);
                    const std::uint32_t i0 = walls.AddVertex(a, n, Vector2(0, ridge), kWhite);
                    const std::uint32_t i1 = walls.AddVertex(b, n, Vector2(hd * 2.0f, ridge), kWhite);
                    const std::uint32_t i2 = walls.AddVertex(t, n, Vector2(hd, 0), kWhite);
                    walls.AddTriangle(i0, i1, i2);
                }
            }
        }

        /// Window quad set into a wall facing `n` (local +z front, -z back, +x right, -x left).
        void Window(MeshData& m, const Vector3& centre, const float w, const float h, const Vector3& n)
        {
            const Vector3 up(0.0f, 1.0f, 0.0f);
            const Vector3 right = Vector3::Cross(up, n);
            const Vector3 o = centre + n * 0.015f;
            const Vector3 a = o - right * (w * 0.5f) - up * (h * 0.5f);
            const Vector3 b = o + right * (w * 0.5f) - up * (h * 0.5f);
            const Vector3 c = o + right * (w * 0.5f) + up * (h * 0.5f);
            const Vector3 d = o - right * (w * 0.5f) + up * (h * 0.5f);
            Quad(m, a, b, c, d, n);
        }

        void WindowRow(MeshData& windows, MeshData& trim, const float hw, const float hd, const float yBottom, const float h, const float w,
                       const float spacing, const Vector3& n, const bool sill, const int skipCentre = -1)
        {
            const bool frontBack = std::fabs(n.Z) > 0.5f;
            const float span = frontBack ? hw : hd;
            const int count = std::max(1, static_cast<int>((span * 2.0f - 0.8f) / spacing));
            for (int i = 0; i < count; ++i) {
                if (i == skipCentre) continue;
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count) - 0.5f;
                const float along = t * (span * 2.0f - 0.8f);
                Vector3 centre = frontBack ? Vector3(along, yBottom + h * 0.5f, n.Z * hd) : Vector3(n.X * hw, yBottom + h * 0.5f, along);
                Window(windows, centre, w, h, n);
                if (sill) {
                    const Vector3 up(0.0f, 1.0f, 0.0f);
                    const Vector3 right = Vector3::Cross(up, n);
                    const Vector3 sc = centre - up * (h * 0.5f + 0.03f) + n * 0.06f;
                    MeshData s;
                    s.AddBox(Vector3(-w * 0.5f - 0.06f, -0.03f, -0.06f), Vector3(w * 0.5f + 0.06f, 0.03f, 0.06f), 1.0f);
                    // Orient: local x along `right`, local z along n.
                    const Matrix basis(right.X, right.Y, right.Z, 0, 0, 1, 0, 0, n.X, n.Y, n.Z, 0, sc.X, sc.Y, sc.Z, 1);
                    trim.Append(s, basis);
                }
            }
        }

        void Door(MeshData& trim, const Vector3& centreBase, const float w, const float h, const Vector3& n)
        {
            const Vector3 up(0.0f, 1.0f, 0.0f);
            const Vector3 right = Vector3::Cross(up, n);
            const Vector3 o = centreBase + n * 0.02f;
            Quad(trim, o - right * (w * 0.5f), o + right * (w * 0.5f), o + right * (w * 0.5f) + up * h, o - right * (w * 0.5f) + up * h, n, 1.0f, 2.0f);
        }

        void Chimney(MeshData& trim, const Vector3& base, const float size, const float height)
        {
            trim.AddBox(base - Vector3(size * 0.5f, 0.0f, size * 0.5f), base + Vector3(size * 0.5f, height, size * 0.5f), 1.0f);
        }
    }

    Rgb BuildingPalette::Wall(const int index)
    {
        static const Rgb palette[kWallColours] = {
            Rgb::FromBytes(232, 214, 178),   // cream
            Rgb::FromBytes(222, 186, 118),   // ochre
            Rgb::FromBytes(236, 226, 164),   // pale yellow
            Rgb::FromBytes(198, 214, 178),   // light green
            Rgb::FromBytes(232, 190, 168),   // salmon
            Rgb::FromBytes(240, 238, 230),   // white
            Rgb::FromBytes(204, 204, 200),   // grey
            Rgb::FromBytes(180, 194, 208),   // blue-grey
        };
        return palette[std::clamp(index, 0, kWallColours - 1)];
    }

    Rgb BuildingPalette::Roof(const int index)
    {
        static const Rgb palette[kRoofColours] = {
            Rgb::FromBytes(168, 76, 54),     // red-brown tile
            Rgb::FromBytes(104, 66, 50),     // dark brown
            Rgb::FromBytes(112, 114, 120),   // grey slate
            Rgb::FromBytes(196, 100, 62),    // orange tile
        };
        return palette[std::clamp(index, 0, kRoofColours - 1)];
    }

    int BuildingPalette::WallIndex(const Map::PlacedBuilding& b)
    {
        const std::string& type = b.spec->type;
        if (type == "block") return 6;
        if (type == "church" || type == "chapel") return 5;
        if (type == "barn") return 1;
        return static_cast<int>(b.spec->seed % 6u);
    }

    int BuildingPalette::RoofIndex(const Map::PlacedBuilding& b)
    {
        const std::string& type = b.spec->type;
        if (type == "block") return 2;
        if (type == "church" || type == "chapel") return 2;
        if (type == "barn") return 1;
        const unsigned r = (b.spec->seed / 7u) % 10u;
        return r < 6 ? 0 : (r < 8 ? 3 : 1);
    }

    Image BuildingGenerator::WindowTexture(const int size, const unsigned seed)
    {
        Image img(size, size);
        const float frame = 0.07f;
        img.Generate([&](int, int, float u, float v) {
            const bool onFrame = u < frame || u > 1.0f - frame || v < frame || v > 1.0f - frame || std::fabs(u - 0.5f) < frame * 0.5f ||
                                 std::fabs(v - 0.42f) < frame * 0.45f;
            if (onFrame) {
                return Color(236, 236, 230, 255);
            }
            // Glass: bluish grey with a sky gradient and a curtain hint at the sides.
            float g = 0.32f + 0.25f * (1.0f - v);
            const float curtain = std::max(0.0f, 0.22f - std::min(u, 1.0f - u)) * 2.0f;
            const float noise = Core::Noise::Value(u * 6.0f, v * 6.0f, 6, seed) * 0.06f;
            const Rgb glass{0.36f * g + 0.05f + noise, 0.42f * g + 0.06f + noise, 0.55f * g + 0.08f};
            const Rgb curtainColour{0.86f, 0.84f, 0.78f};
            const Rgb c = Lerp(glass, curtainColour, std::clamp(curtain, 0.0f, 0.8f));
            return Color(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), 255);
        });
        return img;
    }

    void BuildingGenerator::Generate(const Map::PlacedBuilding& b, BuildingMeshes& out)
    {
        const std::string& type = b.spec->type;
        MeshData walls, roof, windows, trim, glass;
        const float hw = b.halfWidth;
        const float hd = b.halfDepth;
        const float h = b.height;
        const float drop = b.foundationDrop;
        const Vector3 front(0.0f, 0.0f, 1.0f);    // facade faces local +z (rotated into the heading)
        const Vector3 back(0.0f, 0.0f, -1.0f);
        const Vector3 left(-1.0f, 0.0f, 0.0f);
        const Vector3 right(1.0f, 0.0f, 0.0f);
        const int floors = std::max(1, b.spec->floors);
        const float floorH = h / static_cast<float>(floors);

        // Main body.
        Box(walls, Vector3(-hw, -drop, -hd), Vector3(hw, h, hd), 0.35f);
        // Plinth band.
        Box(trim, Vector3(-hw - 0.03f, -drop, -hd - 0.03f), Vector3(hw + 0.03f, 0.45f, hd + 0.03f), 0.5f);

        if (type == "block") {
            // Prefab block: flat roof with parapet, dense window grid, balconies on the front.
            Box(roof, Vector3(-hw - 0.1f, h, -hd - 0.1f), Vector3(hw + 0.1f, h + 0.25f, hd + 0.1f), 0.25f);
            Box(trim, Vector3(-hw + 0.5f, h + 0.25f, -hd + 0.5f), Vector3(-hw + 2.0f, h + 1.4f, -hd + 2.0f), 0.5f);   // lift housing
            for (int f = 0; f < floors; ++f) {
                const float y = static_cast<float>(f) * floorH + 0.9f;
                WindowRow(glass, trim, hw, hd, y, 1.5f, 1.6f, 3.0f, front, false);
                WindowRow(glass, trim, hw, hd, y, 1.5f, 1.6f, 3.0f, back, false);
                if (f > 0) {
                    // Balconies every second bay on the front.
                    const int bays = std::max(1, static_cast<int>((hw * 2.0f - 0.8f) / 3.0f));
                    for (int i = 0; i < bays; i += 2) {
                        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bays) - 0.5f;
                        const float x = t * (hw * 2.0f - 0.8f);
                        Box(trim, Vector3(x - 1.4f, y - 0.9f, hd), Vector3(x + 1.4f, y - 0.75f, hd + 1.2f), 0.5f);
                        Box(trim, Vector3(x - 1.4f, y - 0.75f, hd + 1.1f), Vector3(x + 1.4f, y + 0.25f, hd + 1.2f), 0.5f);
                    }
                }
            }
            Door(trim, Vector3(0.0f, -0.0f, hd), 1.6f, 2.3f, front);
        } else if (type == "church" || type == "chapel") {
            const bool chapel = type == "chapel";
            const float ridge = b.roofHeight * (chapel ? 1.0f : 1.15f);
            Roof(roof, walls, hw, hd, h, ridge, false, 0.4f, 0.12f);
            // Tower at the facade end.
            const float tw = chapel ? hw * 0.9f : std::min(hw * 0.7f, 4.0f);
            const float towerH = h + ridge + (chapel ? 2.0f : 8.0f);
            Box(walls, Vector3(-tw, -drop, hd - tw * 0.5f), Vector3(tw, towerH, hd + tw * 1.5f), 0.35f);
            // Pyramid spire.
            {
                const float top = towerH + (chapel ? 3.0f : 7.0f);
                const Vector3 apex(0.0f, top, hd + tw * 0.5f);
                const Vector3 c0(-tw - 0.2f, towerH, hd - tw * 0.5f - 0.2f);
                const Vector3 c1(tw + 0.2f, towerH, hd - tw * 0.5f - 0.2f);
                const Vector3 c2(tw + 0.2f, towerH, hd + tw * 1.5f + 0.2f);
                const Vector3 c3(-tw - 0.2f, towerH, hd + tw * 1.5f + 0.2f);
                const Vector3 corners[4] = {c0, c1, c2, c3};
                for (int i = 0; i < 4; ++i) {
                    const Vector3& a = corners[i];
                    const Vector3& bb = corners[(i + 1) % 4];
                    Vector3 n = Vector3::Cross(bb - a, apex - a);
                    n.Normalize();
                    const std::uint32_t i0 = roof.AddVertex(a, n, Vector2(0, 4), kWhite);
                    const std::uint32_t i1 = roof.AddVertex(bb, n, Vector2(4, 4), kWhite);
                    const std::uint32_t i2 = roof.AddVertex(apex, n, Vector2(2, 0), kWhite);
                    roof.AddTriangle(i0, i1, i2);
                }
                // Cross.
                Box(trim, Vector3(-0.06f, top, hd + tw * 0.5f - 0.06f), Vector3(0.06f, top + 1.6f, hd + tw * 0.5f + 0.06f), 1.0f);
                Box(trim, Vector3(-0.5f, top + 1.0f, hd + tw * 0.5f - 0.06f), Vector3(0.5f, top + 1.12f, hd + tw * 0.5f + 0.06f), 1.0f);
            }
            // Tall arched-look windows on the nave sides, belfry openings on the tower.
            for (const Vector3& n : {left, right}) {
                WindowRow(windows, trim, hw, hd - tw, h * 0.35f, h * 0.5f, 1.1f, 4.0f, n, false);
            }
            Window(glass, Vector3(0.0f, towerH - 2.0f, hd + tw * 1.5f), 1.0f, 2.2f, front);
            Door(trim, Vector3(0.0f, 0.0f, hd + tw * 1.5f), chapel ? 1.2f : 2.2f, chapel ? 2.2f : 3.6f, front);
        } else {
            const bool hipped = type == "hall" || type == "shop";
            const float ridge = b.roofHeight;
            Roof(roof, walls, hw, hd, h, ridge, hipped, 0.45f, 0.12f);
            Chimney(trim, Vector3(hw * 0.4f, h + ridge * 0.55f, -hd * 0.3f), 0.5f, ridge * 0.6f + 0.8f);
            if (type == "barn") {
                Door(trim, Vector3(0.0f, 0.0f, hd), 3.6f, 3.4f, front);
                WindowRow(windows, trim, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, back, false);
                WindowRow(windows, trim, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, left, false);
            } else {
                for (int f = 0; f < floors; ++f) {
                    const float y = static_cast<float>(f) * floorH + (f == 0 ? 1.0f : 0.95f);
                    const bool shopFront = type == "shop" && f == 0;
                    const int doorSlot = f == 0 ? 0 : -1;
                    if (shopFront) {
                        WindowRow(glass, trim, hw, hd, 0.5f, 2.2f, 2.4f, 3.0f, front, false, 0);
                    } else {
                        WindowRow(windows, trim, hw, hd, y, 1.35f, 1.05f, 2.4f, front, true, doorSlot);
                    }
                    WindowRow(windows, trim, hw, hd, y, 1.35f, 1.05f, 2.4f, back, true);
                    if (hw * 2.0f > 6.0f) {
                        WindowRow(windows, trim, hw, hd, y, 1.35f, 1.05f, 3.2f, left, true);
                        WindowRow(windows, trim, hw, hd, y, 1.35f, 1.05f, 3.2f, right, true);
                    }
                }
                // Door in the first bay of the facade.
                const int count = std::max(1, static_cast<int>((hw * 2.0f - 0.8f) / 2.4f));
                const float t = 0.5f / static_cast<float>(count) - 0.5f;
                Door(trim, Vector3(t * (hw * 2.0f - 0.8f), 0.0f, hd), 1.0f, 2.15f, front);
            }
        }

        // Transform into world space: local +z is the facade direction.
        const Matrix world = Matrix::CreateRotationY(-b.headingRad + 3.14159265f) * Matrix::CreateTranslation(b.position);
        const int wallIndex = BuildingPalette::WallIndex(b);
        const int roofIndex = BuildingPalette::RoofIndex(b);
        out.walls[static_cast<std::size_t>(wallIndex)].Append(walls, world);
        out.roofs[static_cast<std::size_t>(roofIndex)].Append(roof, world);
        out.windows.Append(windows, world);
        out.trim.Append(trim, world);
        out.glassDark.Append(glass, world);
    }
}
