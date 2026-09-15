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


        /// Splits a mesh of window quads (four vertices, six indices each) into the ones whose
        /// light is on after dark and the rest. The choice is a hash of the quad centre and the
        /// building seed, so it is stable between runs and between renderers.
        void SplitLitWindows(const MeshData& in, const unsigned seed, const int litPercent, MeshData& darkOut, MeshData& litOut)
        {
            for (std::size_t q = 0; q + 4 <= in.vertices.size(); q += 4) {
                Vector3 centre(0.0f, 0.0f, 0.0f);
                for (std::size_t k = 0; k < 4; ++k) centre += in.vertices[q + k].position;
                centre *= 0.25f;
                unsigned hash = seed * 2654435761u;
                hash ^= static_cast<unsigned>(static_cast<int>(centre.X * 37.0f)) * 2246822519u;
                hash ^= static_cast<unsigned>(static_cast<int>(centre.Y * 53.0f)) * 3266489917u;
                hash ^= static_cast<unsigned>(static_cast<int>(centre.Z * 41.0f)) * 668265263u;
                hash ^= hash >> 15;
                hash *= 2246822519u;
                hash ^= hash >> 13;
                MeshData& target = static_cast<int>(hash % 100u) < litPercent ? litOut : darkOut;
                const auto base = static_cast<std::uint32_t>(target.vertices.size());
                for (std::size_t k = 0; k < 4; ++k) target.vertices.push_back(in.vertices[q + k]);
                const std::uint32_t order[6] = {0, 1, 2, 0, 2, 3};
                for (const std::uint32_t o : order) target.indices.push_back(base + o);
            }
        }

        /// Local frame on a wall: x along the wall (right), y up, z out along the normal.
        Matrix FaceBasis(const Vector3& n, const Vector3& origin)
        {
            const Vector3 up(0.0f, 1.0f, 0.0f);
            const Vector3 right = Vector3::Cross(up, n);
            return Matrix(right.X, right.Y, right.Z, 0, 0, 1, 0, 0, n.X, n.Y, n.Z, 0, origin.X, origin.Y, origin.Z, 1);
        }

        /// Window frame as geometry: four members 8 cm wide standing 5 cm proud of the wall, and
        /// a dark reveal line under the head so the opening reads recessed.
        void Frame(MeshData& frames, MeshData& dark, const Vector3& centre, const float w, const float h, const Vector3& n)
        {
            const float fw = 0.08f, depth = 0.05f;
            MeshData f;
            f.AddBox(Vector3(-w * 0.5f - fw, -h * 0.5f - fw, 0.0f), Vector3(-w * 0.5f, h * 0.5f + fw, depth), 1.0f);
            f.AddBox(Vector3(w * 0.5f, -h * 0.5f - fw, 0.0f), Vector3(w * 0.5f + fw, h * 0.5f + fw, depth), 1.0f);
            f.AddBox(Vector3(-w * 0.5f, h * 0.5f, 0.0f), Vector3(w * 0.5f, h * 0.5f + fw, depth), 1.0f);
            f.AddBox(Vector3(-w * 0.5f, -h * 0.5f - fw, 0.0f), Vector3(w * 0.5f, -h * 0.5f, depth), 1.0f);
            frames.Append(f, FaceBasis(n, centre + n * 0.004f));
            MeshData d;
            d.AddQuad(Vector3(-w * 0.5f, h * 0.5f - 0.11f, 0.0f), Vector3(w * 0.5f, h * 0.5f - 0.11f, 0.0f), Vector3(w * 0.5f, h * 0.5f, 0.0f),
                      Vector3(-w * 0.5f, h * 0.5f, 0.0f), Vector3(0, 0, 1), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), kWhite);
            dark.Append(d, FaceBasis(n, centre + n * 0.022f));
        }

        /// Fascia board and gutter along one eave from `a` to `b` (both at eaves height), the
        /// gutter hanging just below the board; a downpipe drops to the ground at `a`.
        void Eave(MeshData& frames, MeshData& metal, const Vector3& a, const Vector3& b, const Vector3& outward, const float groundY, const bool downpipe)
        {
            const Vector3 along = b - a;
            const float length = along.Length();
            if (length < 0.5f) return;
            const Vector3 dir = along * (1.0f / length);
            MeshData f;
            f.AddBox(Vector3(0.0f, -0.16f, 0.0f), Vector3(length, 0.02f, 0.035f), 1.0f);
            const Matrix basis(dir.X, dir.Y, dir.Z, 0, 0, 1, 0, 0, outward.X, outward.Y, outward.Z, 0, a.X + outward.X * 0.01f, a.Y, a.Z + outward.Z * 0.01f, 1);
            frames.Append(f, basis);
            metal.AddCylinder(a + outward * 0.09f + Vector3(0.0f, -0.20f, 0.0f), dir, 0.055f, length, 8, true);
            if (downpipe) {
                const Vector3 foot = a + dir * 0.35f + outward * 0.10f;
                metal.AddCylinder(Vector3(foot.X, groundY, foot.Z), Vector3(0, 1, 0), 0.04f, a.Y - 0.22f - groundY, 6, false);
            }
        }

        /// Gable or hipped roof over a rectangle x in [-hw, hw], z in [-hd, hd] at eaves height `y0`,
        /// with ridge tiles, fascia boards, gutters and downpipes.
        void Roof(MeshData& roof, MeshData& walls, MeshData& frames, MeshData& metal, const float hw, const float hd, const float y0,
                  const float ridge, const bool hipped, const float overhang, const float thickness, const float groundY)
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
            // Ridge tiles and the eaves kit.
            roof.AddBox(Vector3(ridgeL.X, y0 + ridge - 0.03f, -0.12f), Vector3(ridgeR.X, y0 + ridge + 0.07f, 0.12f), 0.5f);
            Eave(frames, metal, Vector3(-ox, yEave, oz), Vector3(ox, yEave, oz), Vector3(0, 0, 1), groundY, true);
            Eave(frames, metal, Vector3(ox, yEave, -oz), Vector3(-ox, yEave, -oz), Vector3(0, 0, -1), groundY, true);
            if (hipped) {
                Eave(frames, metal, Vector3(ox, yEave, oz), Vector3(ox, yEave, -oz), Vector3(1, 0, 0), groundY, false);
                Eave(frames, metal, Vector3(-ox, yEave, -oz), Vector3(-ox, yEave, oz), Vector3(-1, 0, 0), groundY, false);
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

        void WindowRow(MeshData& windows, MeshData& trim, MeshData* frames, MeshData* dark, const float hw, const float hd, const float yBottom,
                       const float h, const float w, const float spacing, const Vector3& n, const bool sill, const int skipCentre = -1)
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
                if (frames && dark) Frame(*frames, *dark, centre, w, h, n);
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

        void Chimney(MeshData& trim, MeshData& frames, MeshData& dark, const Vector3& base, const float size, const float height)
        {
            trim.AddBox(base - Vector3(size * 0.5f, 0.0f, size * 0.5f), base + Vector3(size * 0.5f, height, size * 0.5f), 1.0f);
            frames.AddBox(base + Vector3(-size * 0.5f - 0.08f, height, -size * 0.5f - 0.08f), base + Vector3(size * 0.5f + 0.08f, height + 0.08f, size * 0.5f + 0.08f), 1.0f);
            dark.AddCylinder(base + Vector3(0.0f, height + 0.08f, 0.0f), Vector3(0, 1, 0), 0.09f, 0.24f, 8, true);
        }

        /// Gabled dormer on the front slope: a wall box breaking the slope, a small roof of two
        /// tilted slabs, a framed window.
        void Dormer(MeshData& walls, MeshData& roof, MeshData& frames, MeshData& dark, MeshData& windows, const float x, const float y0,
                    const float ridge, const float hd)
        {
            const float zFront = hd * 0.50f + 0.12f;
            const float ySlope = y0 + ridge * (1.0f - 0.50f);
            const float half = 0.75f, height = 1.25f, back = 1.5f;
            walls.AddBox(Vector3(x - half, ySlope - 0.5f, zFront - back), Vector3(x + half, ySlope + height, zFront), 0.35f);
            const float yTop = ySlope + height + 0.55f;
            const float slopeLen = std::hypot(half + 0.15f, 0.55f);
            const float theta = std::atan2(0.55f, half + 0.15f);
            for (const float side : {1.0f, -1.0f}) {
                MeshData slab;
                slab.AddBox(Vector3(0.0f, -0.07f, zFront - back - 0.05f), Vector3(slopeLen, 0.0f, zFront + 0.25f), 0.5f);
                slab.Transform(Matrix::CreateRotationZ(side > 0.0f ? -theta : 3.14159265f + theta) * Matrix::CreateTranslation(x, yTop, 0.0f));
                roof.Append(slab, Matrix::getIdentityProperty());
            }
            const Vector3 centre(x, ySlope + 0.60f, zFront);
            Window(windows, centre, 0.9f, 0.9f, Vector3(0, 0, 1));
            Frame(frames, dark, centre, 0.9f, 0.9f, Vector3(0, 0, 1));
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
        MeshData walls, roof, windows, trim, glass, frames, metal, dark, concrete;
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
            Box(concrete, Vector3(-hw + 0.5f, h + 0.25f, -hd + 0.5f), Vector3(-hw + 2.0f, h + 1.4f, -hd + 2.0f), 0.5f);   // lift housing
            for (int f = 0; f < floors; ++f) {
                const float y = static_cast<float>(f) * floorH + 0.9f;
                WindowRow(glass, trim, &frames, &dark, hw, hd, y, 1.5f, 1.6f, 3.0f, front, false);
                WindowRow(glass, trim, &frames, &dark, hw, hd, y, 1.5f, 1.6f, 3.0f, back, false);
                // Gable ends: a pair of small windows per floor (stairwell and bathroom), so the
                // end wall is not a blank slab.
                WindowRow(glass, trim, &frames, &dark, hw, hd, y + 0.15f, 1.1f, 0.9f, 4.0f, left, false);
                WindowRow(glass, trim, &frames, &dark, hw, hd, y + 0.15f, 1.1f, 0.9f, 4.0f, right, false);
                if (f > 0) {
                    // Loggias every second bay: concrete slab, concrete parapet, steel handrail.
                    // The back row is offset by one bay so the two facades do not look identical.
                    const int bays = std::max(1, static_cast<int>((hw * 2.0f - 0.8f) / 3.0f));
                    for (int i = 0; i < bays; i += 2) {
                        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bays) - 0.5f;
                        const float x = t * (hw * 2.0f - 0.8f);
                        Box(concrete, Vector3(x - 1.4f, y - 0.9f, hd), Vector3(x + 1.4f, y - 0.75f, hd + 1.2f), 0.5f);
                        Box(concrete, Vector3(x - 1.4f, y - 0.75f, hd + 1.1f), Vector3(x + 1.4f, y + 0.20f, hd + 1.2f), 0.5f);
                        metal.AddBox(Vector3(x - 1.42f, y + 0.20f, hd + 1.08f), Vector3(x + 1.42f, y + 0.25f, hd + 1.22f), 1.0f);
                    }
                    for (int i = 1; i < bays; i += 2) {
                        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bays) - 0.5f;
                        const float x = t * (hw * 2.0f - 0.8f);
                        Box(concrete, Vector3(x - 1.4f, y - 0.9f, -hd - 1.2f), Vector3(x + 1.4f, y - 0.75f, -hd), 0.5f);
                        Box(concrete, Vector3(x - 1.4f, y - 0.75f, -hd - 1.2f), Vector3(x + 1.4f, y + 0.20f, -hd - 1.1f), 0.5f);
                        metal.AddBox(Vector3(x - 1.42f, y + 0.20f, -hd - 1.22f), Vector3(x + 1.42f, y + 0.25f, -hd - 1.08f), 1.0f);
                    }
                }
            }
            Door(trim, Vector3(0.0f, -0.0f, hd), 1.6f, 2.3f, front);
            Box(concrete, Vector3(-1.3f, 2.35f, hd), Vector3(1.3f, 2.5f, hd + 1.5f), 0.5f);   // entrance canopy
            Box(concrete, Vector3(-1.3f, -drop, hd), Vector3(1.3f, 0.02f, hd + 1.2f), 0.5f);  // entrance slab
        } else if (type == "church" || type == "chapel") {
            const bool chapel = type == "chapel";
            const float ridge = b.roofHeight * (chapel ? 1.0f : 1.15f);
            Roof(roof, walls, frames, metal, hw, hd, h, ridge, false, 0.4f, 0.12f, -drop + 0.3f);
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
                WindowRow(windows, trim, &frames, &dark, hw, hd - tw, h * 0.35f, h * 0.5f, 1.1f, 4.0f, n, false);
            }
            Window(glass, Vector3(0.0f, towerH - 2.0f, hd + tw * 1.5f), 1.0f, 2.2f, front);
            Door(trim, Vector3(0.0f, 0.0f, hd + tw * 1.5f), chapel ? 1.2f : 2.2f, chapel ? 2.2f : 3.6f, front);
        } else {
            const unsigned seed = b.spec->seed;
            const bool hipped = type == "hall" || type == "shop" || (type == "house" && seed % 5u == 0u);
            const float ridge = b.roofHeight;
            Roof(roof, walls, frames, metal, hw, hd, h, ridge, hipped, 0.45f, 0.12f, -drop + 0.3f);
            Chimney(trim, frames, dark, Vector3(hw * 0.4f, h + ridge * 0.55f, -hd * 0.3f), 0.5f, ridge * 0.6f + 0.8f);
            if (type == "barn") {
                Door(trim, Vector3(0.0f, 0.0f, hd), 3.6f, 3.4f, front);
                WindowRow(windows, trim, nullptr, nullptr, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, back, false);
                WindowRow(windows, trim, nullptr, nullptr, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, left, false);
            } else {
                for (int f = 0; f < floors; ++f) {
                    const float y = static_cast<float>(f) * floorH + (f == 0 ? 1.0f : 0.95f);
                    const bool shopFront = type == "shop" && f == 0;
                    const int doorSlot = f == 0 ? 0 : -1;
                    if (shopFront) {
                        WindowRow(glass, trim, &frames, &dark, hw, hd, 0.5f, 2.2f, 2.4f, 3.0f, front, false, 0);
                    } else {
                        WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.35f, 1.05f, 2.4f, front, true, doorSlot);
                    }
                    WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.35f, 1.05f, 2.4f, back, true);
                    if (hw * 2.0f > 6.0f) {
                        WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.35f, 1.05f, 3.2f, left, true);
                        WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.35f, 1.05f, 3.2f, right, true);
                    }
                }
                // Door in the first bay of the facade, with a doorstep and a small canopy.
                const int count = std::max(1, static_cast<int>((hw * 2.0f - 0.8f) / 2.4f));
                const float t = 0.5f / static_cast<float>(count) - 0.5f;
                const float doorX = t * (hw * 2.0f - 0.8f);
                Door(trim, Vector3(doorX, 0.0f, hd), 1.0f, 2.15f, front);
                Box(concrete, Vector3(doorX - 0.8f, -drop, hd), Vector3(doorX + 0.8f, 0.03f, hd + 0.9f), 0.5f);
                Box(frames, Vector3(doorX - 0.85f, 2.27f, hd), Vector3(doorX + 0.85f, 2.35f, hd + 0.75f), 1.0f);
                // Cornice under the eaves and a string course between floors on town houses.
                if (floors >= 2) {
                    Box(frames, Vector3(-hw - 0.05f, h - 0.24f, -hd - 0.05f), Vector3(hw + 0.05f, h - 0.08f, hd + 0.05f), 1.0f);
                    Box(frames, Vector3(-hw - 0.03f, floorH - 0.04f, -hd - 0.03f), Vector3(hw + 0.03f, floorH + 0.04f, hd + 0.03f), 1.0f);
                }
                // A dormer on some two-storey gabled houses.
                if (type == "house" && floors >= 2 && !hipped && seed % 3u == 1u && hw > 4.5f) {
                    Dormer(walls, roof, frames, dark, windows, hw * 0.25f, h, ridge, hd);
                }
            }
        }

        // Transform into world space: local +z is the facade direction.
        const Matrix world = Matrix::CreateRotationY(-b.headingRad + 3.14159265f) * Matrix::CreateTranslation(b.position);
        const int wallIndex = BuildingPalette::WallIndex(b);
        const int roofIndex = BuildingPalette::RoofIndex(b);
        out.walls[static_cast<std::size_t>(wallIndex)].Append(walls, world);
        out.roofs[static_cast<std::size_t>(roofIndex)].Append(roof, world);
        // After dark roughly two windows in five are lit; the rest stay as they are.
        const unsigned litSeed = b.spec->seed * 2654435761u ^ (static_cast<unsigned>(b.position.X * 7.0f) << 8) ^
                                 static_cast<unsigned>(b.position.Z * 11.0f);
        MeshData windowsDark, windowsLit, glassDarkOnly, glassLit;
        SplitLitWindows(windows, litSeed, 40, windowsDark, windowsLit);
        SplitLitWindows(glass, litSeed + 17u, 35, glassDarkOnly, glassLit);
        out.windows.Append(windowsDark, world);
        out.windowsLit.Append(windowsLit, world);
        out.trim.Append(trim, world);
        out.glassDark.Append(glassDarkOnly, world);
        out.glassLit.Append(glassLit, world);
        out.frames.Append(frames, world);
        out.metal.Append(metal, world);
        out.dark.Append(dark, world);
        out.concrete.Append(concrete, world);
    }
}
