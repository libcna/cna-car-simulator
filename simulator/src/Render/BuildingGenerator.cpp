#include "CarSim/Render/BuildingGenerator.hpp"

#include "CarSim/Core/Noise.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        const Color kWhite(255, 255, 255, 255);

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
            // The window cross: a centre mullion and a transom at two thirds of the height, the
            // way Czech casement windows are divided. Narrow openings get the transom only.
            const float bar = 0.035f, barDepth = 0.035f;
            const float transomY = h * (2.0f / 3.0f) - h * 0.5f;
            f.AddBox(Vector3(-w * 0.5f, transomY - bar, 0.0f), Vector3(w * 0.5f, transomY + bar, barDepth), 1.0f);
            if (w > 0.8f) {
                f.AddBox(Vector3(-bar, -h * 0.5f, 0.0f), Vector3(bar, h * 0.5f, barDepth), 1.0f);
            }
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
                       const float h, const float w, const float spacing, const Vector3& n, const bool sill, const int skipCentre = -1,
                       const bool surround = false, const bool shutters = false)
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
                if (shutters) {
                    // Folded timber shutters change the street silhouette on selected houses
                    // and cottages. They sit beside the glass and share the existing dark-wood
                    // trim batch rather than covering the functioning window surface.
                    MeshData leaves;
                    for (const float side : {-1.0f, 1.0f}) {
                        const float x0 = side * (w * 0.5f + 0.10f);
                        const float x1 = side * (w * 0.5f + 0.34f);
                        leaves.AddBox(Vector3(std::min(x0, x1), -h * 0.5f, 0.0f),
                                      Vector3(std::max(x0, x1), h * 0.5f, 0.045f), 1.0f);
                    }
                    trim.Append(leaves, FaceBasis(n, centre + n * 0.055f));
                }
                if (surround && frames) {
                    // Town-house window surround: a flat plaster band round the opening and a
                    // small cornice (the head moulding) over it that throws a shadow line.
                    MeshData s;
                    const float band = 0.13f, proud = 0.025f;
                    s.AddBox(Vector3(-w * 0.5f - 0.08f - band, -h * 0.5f - 0.08f, 0.0f), Vector3(-w * 0.5f - 0.08f, h * 0.5f + 0.08f, proud), 1.0f);
                    s.AddBox(Vector3(w * 0.5f + 0.08f, -h * 0.5f - 0.08f, 0.0f), Vector3(w * 0.5f + 0.08f + band, h * 0.5f + 0.08f, proud), 1.0f);
                    s.AddBox(Vector3(-w * 0.5f - 0.08f - band, h * 0.5f + 0.08f, 0.0f), Vector3(w * 0.5f + 0.08f + band, h * 0.5f + 0.08f + band, proud), 1.0f);
                    s.AddBox(Vector3(-w * 0.5f - 0.30f, h * 0.5f + 0.08f + band, 0.0f), Vector3(w * 0.5f + 0.30f, h * 0.5f + 0.2f + band, 0.12f), 1.0f);
                    frames->Append(s, FaceBasis(n, centre));
                }
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

        void Door(MeshData& trim, MeshData& frames, MeshData& metal, MeshData& dark,
                  const Vector3& centreBase, const float w, const float h, const Vector3& n)
        {
            const Vector3 up(0.0f, 1.0f, 0.0f);
            const Vector3 right = Vector3::Cross(up, n);
            const Vector3 o = centreBase + n * 0.02f;
            Quad(trim, o - right * (w * 0.5f), o + right * (w * 0.5f), o + right * (w * 0.5f) + up * h, o - right * (w * 0.5f) + up * h, n, 1.0f, 2.0f);

            // A single shallow kit works for cottages, shop/service doors and church entrances.
            // It joins the existing per-chunk trim/material meshes, so hundreds of doors add
            // geometry but no new material submissions. The proud frame and inset panel edges
            // are especially important when a player walks up to an otherwise flat facade.
            MeshData casing, panel, grooves, fittings;
            const float jamb = std::clamp(w * 0.10f, 0.08f, 0.15f);
            const float depth = 0.10f;
            casing.AddBox(Vector3(-w * 0.5f - jamb, 0.0f, 0.0f), Vector3(-w * 0.5f, h + jamb, depth), 1.0f);
            casing.AddBox(Vector3(w * 0.5f, 0.0f, 0.0f), Vector3(w * 0.5f + jamb, h + jamb, depth), 1.0f);
            casing.AddBox(Vector3(-w * 0.5f - jamb, h, 0.0f), Vector3(w * 0.5f + jamb, h + jamb, depth + 0.035f), 1.0f);
            casing.AddBox(Vector3(-w * 0.5f - jamb, -0.025f, 0.0f), Vector3(w * 0.5f + jamb, 0.035f, depth + 0.09f), 1.0f);

            const int leaves = w > 1.45f ? 2 : 1;
            const float leafW = w / static_cast<float>(leaves);
            for (int leaf = 0; leaf < leaves; ++leaf) {
                const float x = -w * 0.5f + (static_cast<float>(leaf) + 0.5f) * leafW;
                const float panelHalf = std::max(0.12f, leafW * 0.5f - 0.14f);
                const float inset = h > 2.7f ? 0.22f : 0.16f;
                const float split = h * 0.52f;
                for (const auto& [bottom, top] : {std::pair{inset, split - 0.09f}, std::pair{split + 0.09f, h - inset}}) {
                    if (top <= bottom) continue;
                    grooves.AddBox(Vector3(x - panelHalf - 0.025f, bottom - 0.025f, 0.019f),
                                   Vector3(x + panelHalf + 0.025f, top + 0.025f, 0.030f), 1.0f);
                    panel.AddBox(Vector3(x - panelHalf, bottom, 0.030f), Vector3(x + panelHalf, top, 0.055f), 1.0f);
                }
            }
            const float handleX = leaves == 2 ? -0.055f : w * 0.5f - 0.14f;
            const float handleY = std::min(1.02f, h * 0.48f);
            fittings.AddCylinder(Vector3(handleX, handleY, 0.07f), Vector3(0, 0, 1), 0.025f, 0.035f, 8, true);
            fittings.AddCylinder(Vector3(handleX - 0.075f, handleY, 0.105f), Vector3(1, 0, 0), 0.014f, 0.11f, 8, true);
            fittings.AddBox(Vector3(-w * 0.5f + 0.055f, 0.05f, 0.055f),
                            Vector3(w * 0.5f - 0.055f, 0.19f, 0.065f), 1.0f);
            const Matrix basis = FaceBasis(n, centreBase + n * 0.025f);
            frames.Append(casing, basis);
            trim.Append(panel, basis);
            dark.Append(grooves, basis);
            metal.Append(fittings, basis);
        }

        /// A small block-letter alphabet for the fascia of the existing shop mesh.
        std::array<unsigned char, 7> ShopLetter(const char ch)
        {
            switch (ch) {
                case 'A': return {14, 17, 17, 31, 17, 17, 17};
                case 'D': return {30, 17, 17, 17, 17, 17, 30};
                case 'E': return {31, 16, 16, 30, 16, 16, 31};
                case 'H': return {17, 17, 17, 31, 17, 17, 17};
                case 'I': return {31, 4, 4, 4, 4, 4, 31};
                case 'K': return {17, 18, 20, 24, 20, 18, 17};
                case 'L': return {16, 16, 16, 16, 16, 16, 31};
                case 'N': return {17, 25, 21, 19, 17, 17, 17};
                case 'O': return {14, 17, 17, 17, 17, 17, 14};
                case 'P': return {30, 17, 17, 30, 16, 16, 16};
                case 'R': return {30, 17, 17, 30, 20, 18, 17};
                case 'T': return {31, 4, 4, 4, 4, 4, 4};
                case 'V': return {17, 17, 17, 17, 17, 10, 4};
                case 'Y': return {17, 17, 10, 4, 4, 4, 4};
                default: return {};
            }
        }

        void ShopSign(MeshData& frames, MeshData& dark, const unsigned seed, const float hd)
        {
            constexpr std::array<std::string_view, 4> names = {"POTRAVINY", "ELEKTRO", "OPRAVY", "HODINY"};
            const std::string_view name = names[seed % names.size()];
            constexpr float pixel = 0.043f;
            const float width = (static_cast<float>(name.size() * 6 - 1)) * pixel;
            const float left = -width * 0.5f;
            // A shallow pale board makes the lettering legible in shade without creating
            // another material batch or covering the full-length dark fascia.
            Box(frames, Vector3(left - 0.17f, 2.81f, hd + 0.108f),
                Vector3(-left + 0.17f, 3.18f, hd + 0.145f), 1.0f);
            const float z = hd + 0.153f;
            for (std::size_t letter = 0; letter < name.size(); ++letter) {
                const auto rows = ShopLetter(name[letter]);
                for (int row = 0; row < 7; ++row) {
                    for (int col = 0; col < 5; ++col) {
                        if ((rows[static_cast<std::size_t>(row)] & (1u << (4 - col))) == 0u) continue;
                        const float x = left + (static_cast<float>(letter) * 6.0f + static_cast<float>(col)) * pixel;
                        const float top = 3.14f - static_cast<float>(row) * pixel;
                        Quad(dark, Vector3(x, top - pixel, z), Vector3(x + pixel, top - pixel, z),
                             Vector3(x + pixel, top, z), Vector3(x, top, z), Vector3(0, 0, 1));
                    }
                }
            }
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

        // ------------------------------------------------------------------ castle pieces
        constexpr float kStoneTileM = 3.0f;   // one masonry texture tile

        /// Merlons standing on the outer edge of a straight parapet from x0 to x1 (local x), at
        /// height y, the outer face at z = zOuter (facing +z).
        void Merlons(MeshData& walls, const float x0, const float x1, const float y, const float zOuter, const Matrix& place)
        {
            MeshData m;
            const float pitch = 1.7f, width = 0.95f;
            const int n = std::max(1, static_cast<int>((x1 - x0) / pitch));
            const float step = (x1 - x0) / static_cast<float>(n);
            for (int i = 0; i < n; ++i) {
                const float x = x0 + step * (static_cast<float>(i) + 0.5f);
                m.AddBox(Vector3(x - width * 0.5f, y, zOuter - 0.55f), Vector3(x + width * 0.5f, y + 0.95f, zOuter), kStoneTileM);
            }
            walls.Append(m, place);
        }

        /// Arrow slits: narrow dark openings on the +z face at z = zFace.
        void Slits(MeshData& dark, const float x0, const float x1, const float y, const float zFace, const float spacing, const Matrix& place)
        {
            MeshData m;
            const int n = std::max(1, static_cast<int>((x1 - x0) / spacing));
            const float step = (x1 - x0) / static_cast<float>(n);
            for (int i = 0; i < n; ++i) {
                const float x = x0 + step * (static_cast<float>(i) + 0.5f);
                m.AddBox(Vector3(x - 0.09f, y, zFace), Vector3(x + 0.09f, y + 1.15f, zFace + 0.04f), 1.0f);
            }
            dark.Append(m, place);
        }

        /// Battlements round a rectangle (all four sides) with their parapet walk behind.
        void Battlements(MeshData& walls, const float hw, const float hd, const float y)
        {
            const Matrix sides[4] = {Matrix::getIdentityProperty(), Matrix::CreateRotationY(3.14159265f),
                                     Matrix::CreateRotationY(3.14159265f * 0.5f), Matrix::CreateRotationY(-3.14159265f * 0.5f)};
            const float along[4] = {hw, hw, hd, hd};
            const float out[4] = {hd, hd, hw, hw};
            for (int s = 0; s < 4; ++s) {
                MeshData parapet;
                parapet.AddBox(Vector3(-along[s], y, out[s] - 0.55f), Vector3(along[s], y + 1.0f, out[s]), kStoneTileM);
                walls.Append(parapet, sides[s]);
                Merlons(walls, -along[s], along[s], y + 1.0f, out[s], sides[s]);
            }
        }

        /// A cone (both windings, so it shows from any side) from a ring of `radius` at y0 up to
        /// the apex at y1.
        void Cone(MeshData& roof, const float radius, const float y0, const float y1, const int segments)
        {
            const Vector3 apex(0.0f, y1, 0.0f);
            for (int i = 0; i < segments; ++i) {
                const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
                const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
                const Vector3 p0(std::cos(a0) * radius, y0, std::sin(a0) * radius);
                const Vector3 p1(std::cos(a1) * radius, y0, std::sin(a1) * radius);
                const float am = 0.5f * (a0 + a1);
                Vector3 n(std::cos(am) * (y1 - y0), radius, std::sin(am) * (y1 - y0));
                n.Normalize();
                const float slant = std::hypot(radius, y1 - y0);
                const float u0 = radius * a0 / 2.0f, u1 = radius * a1 / 2.0f;
                const std::uint32_t i0 = roof.AddVertex(p0, n, Vector2(u0, slant), kWhite);
                const std::uint32_t i1 = roof.AddVertex(p1, n, Vector2(u1, slant), kWhite);
                const std::uint32_t i2 = roof.AddVertex(apex, n, Vector2(0.5f * (u0 + u1), 0.0f), kWhite);
                roof.AddTriangle(i0, i1, i2);
                roof.AddTriangle(i0, i2, i1);
            }
        }

        void CastlePiece(const Map::PlacedBuilding& b, MeshData& walls, MeshData& roof, MeshData& windows, MeshData& trim,
                         MeshData& frames, MeshData& metal, MeshData& dark)
        {
            const std::string& type = b.spec->type;
            const float hw = b.halfWidth, hd = b.halfDepth, h = b.height, drop = b.foundationDrop, ridge = b.roofHeight;
            const Matrix id = Matrix::getIdentityProperty();
            if (type == "castle_wall") {
                // Curtain wall: the masonry, a breastwork with merlons on the outer (+z) side, a
                // low rail on the inner side, slits in the outer face.
                walls.AddBox(Vector3(-hw, -drop, -hd), Vector3(hw, h, hd), kStoneTileM);
                walls.AddBox(Vector3(-hw, h, hd - 0.55f), Vector3(hw, h + 1.0f, hd), kStoneTileM);
                walls.AddBox(Vector3(-hw, h, -hd), Vector3(hw, h + 0.5f, -hd + 0.35f), kStoneTileM);
                Merlons(walls, -hw, hw, h + 1.0f, hd, id);
                Slits(dark, -hw + 2.0f, hw - 2.0f, h * 0.45f, hd, 5.5f, id);
            } else if (type == "castle_tower") {
                // Round tower: a slightly battered drum, a corbelled crown, a tall cone.
                const float r = std::min(hw, hd);
                walls.AddCylinder(Vector3(0.0f, -drop, 0.0f), Vector3(0.0f, 1.0f, 0.0f), r + 0.25f, drop + 2.0f, 18, false,
                                  Color(255, 255, 255, 255), kStoneTileM);
                walls.AddCylinder(Vector3(0.0f, 1.9f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), r, h - 3.0f, 18, false, Color(255, 255, 255, 255), kStoneTileM);
                walls.AddCylinder(Vector3(0.0f, h - 1.2f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), r + 0.4f, 1.2f, 18, true, Color(255, 255, 255, 255), kStoneTileM);
                Cone(roof, r + 0.7f, h - 0.1f, h + std::max(ridge, 3.0f), 18);
                for (int k = 0; k < 4; ++k) {
                    const Matrix turn = Matrix::CreateRotationY(0.4f + 1.5708f * static_cast<float>(k));
                    MeshData slit;
                    for (const float y : {h * 0.35f, h * 0.65f}) {
                        slit.AddBox(Vector3(-0.09f, y, r - 0.02f), Vector3(0.09f, y + 1.1f, r + 0.03f), 1.0f);
                    }
                    dark.Append(slit, turn);
                }
            } else if (type == "castle_keep") {
                // Keep (bergfried): a tall square tower, a corbelled crown with battlements and a
                // steep hipped roof inside them; slits and a door high up the front.
                walls.AddBox(Vector3(-hw, -drop, -hd), Vector3(hw, h, hd), kStoneTileM);
                walls.AddBox(Vector3(-hw - 0.35f, h - 0.9f, -hd - 0.35f), Vector3(hw + 0.35f, h, hd + 0.35f), kStoneTileM);
                Battlements(walls, hw + 0.35f, hd + 0.35f, h);
                if (ridge > 0.1f) Roof(roof, walls, frames, metal, hw - 0.3f, hd - 0.3f, h + 0.4f, ridge, true, 0.2f, 0.12f, h);
                const Matrix faces[4] = {id, Matrix::CreateRotationY(3.14159265f), Matrix::CreateRotationY(1.5708f), Matrix::CreateRotationY(-1.5708f)};
                for (int s = 0; s < 4; ++s) {
                    const float along = s < 2 ? hw : hd;
                    const float out = s < 2 ? hd : hw;
                    for (const float y : {h * 0.3f, h * 0.55f, h * 0.8f}) Slits(dark, -along + 1.5f, along - 1.5f, y, out, 3.5f, faces[s]);
                }
                trim.AddBox(Vector3(-0.7f, 8.0f, hd), Vector3(0.7f, 10.2f, hd + 0.05f), 1.0f);
            } else if (type == "castle_gate") {
                // Gatehouse: two towers with the passage between them, the chamber over the
                // arch, battlements, and the portcullis hanging in the mouth.
                const float gap = b.PassageHalfWidth();
                walls.AddBox(Vector3(-hw, -drop, -hd), Vector3(-gap, h, hd), kStoneTileM);
                walls.AddBox(Vector3(gap, -drop, -hd), Vector3(hw, h, hd), kStoneTileM);
                walls.AddBox(Vector3(-gap, 5.2f, -hd), Vector3(gap, h, hd), kStoneTileM);
                dark.AddBox(Vector3(-gap + 0.02f, 5.05f, -hd + 0.02f), Vector3(gap - 0.02f, 5.2f, hd - 0.02f), 1.0f);
                for (float x = -gap + 0.35f; x < gap - 0.2f; x += 0.45f) {
                    metal.AddBox(Vector3(x - 0.04f, 4.1f, hd - 0.35f), Vector3(x + 0.04f, 5.1f, hd - 0.27f), 1.0f);
                }
                metal.AddBox(Vector3(-gap, 4.2f, hd - 0.35f), Vector3(gap, 4.3f, hd - 0.27f), 1.0f);
                Battlements(walls, hw, hd, h);
                Slits(dark, -hw + 0.8f, -gap - 0.8f, h * 0.55f, hd, 2.0f, id);
                Slits(dark, gap + 0.8f, hw - 0.8f, h * 0.55f, hd, 2.0f, id);
            } else {
                // Palace (palác): the lord's hall, stone, a steep tiled roof, tall paired windows
                // on the upper floors, slits below, a door on the courtyard side.
                walls.AddBox(Vector3(-hw, -drop, -hd), Vector3(hw, h, hd), kStoneTileM);
                Roof(roof, walls, frames, metal, hw, hd, h, ridge, false, 0.35f, 0.12f, -drop + 0.3f);
                const int floors = std::max(1, b.spec->floors);
                const float floorH = h / static_cast<float>(floors);
                for (int f = 1; f < floors; ++f) {
                    const float y = static_cast<float>(f) * floorH + 0.8f;
                    WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.9f, 0.75f, 3.4f, Vector3(0, 0, 1), true);
                    WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.9f, 0.75f, 3.4f, Vector3(0, 0, -1), true);
                }
                Slits(dark, -hw + 2.0f, hw - 2.0f, 1.6f, hd, 4.0f, id);
                Door(trim, frames, metal, dark, Vector3(0.0f, 0.0f, hd), 1.8f, 2.8f, Vector3(0, 0, 1));
            }
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
            Rgb::FromBytes(168, 160, 146),   // weathered stone (castle masonry)
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
        if (type.rfind("castle_", 0) == 0) return kStoneWall;
        if (type == "block") return 6;
        if (type == "church" || type == "chapel") return 5;
        if (type == "barn") return 1;
        return static_cast<int>(b.spec->seed % 6u);
    }

    int BuildingPalette::RoofIndex(const Map::PlacedBuilding& b)
    {
        const std::string& type = b.spec->type;
        if (type == "castle_palace") return 0;
        if (type.rfind("castle_", 0) == 0) return 2;
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

        if (type.rfind("castle_", 0) == 0) {
            // Castle pieces are masonry throughout, with their own shapes.
            CastlePiece(b, walls, roof, windows, trim, frames, metal, dark);
            const Matrix place = Matrix::CreateRotationY(-b.headingRad + 3.14159265f) * Matrix::CreateTranslation(b.position);
            out.walls[static_cast<std::size_t>(BuildingPalette::WallIndex(b))].Append(walls, place);
            out.roofs[static_cast<std::size_t>(BuildingPalette::RoofIndex(b))].Append(roof, place);
            out.windows.Append(windows, place);
            out.trim.Append(trim, place);
            out.frames.Append(frames, place);
            out.metal.Append(metal, place);
            out.dark.Append(dark, place);
            (void)glass;
            (void)concrete;
            (void)front;
            (void)back;
            (void)left;
            (void)right;
            return;
        }

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
            Door(trim, frames, metal, dark, Vector3(0.0f, -0.0f, hd), 1.6f, 2.3f, front);
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
            Door(trim, frames, metal, dark, Vector3(0.0f, 0.0f, hd + tw * 1.5f), chapel ? 1.2f : 2.2f, chapel ? 2.2f : 3.6f, front);
            // The tower faces the village square. Shallow plaster pilasters and a small
            // circular window give this otherwise uninterrupted wall a human-scale rhythm.
            // They join the existing trim/material meshes, so no extra draw pass is needed.
            const float towerFront = hd + tw * 1.5f;
            const float reveal = chapel ? 0.08f : 0.13f;
            const float bandY = chapel ? 3.25f : 4.45f;
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * (tw - 0.38f);
                Box(frames, Vector3(x - 0.16f, 0.45f, towerFront - 0.01f),
                    Vector3(x + 0.16f, std::min(towerH - 1.0f, h + ridge * 0.7f), towerFront + reveal), 1.0f);
                Box(frames, Vector3(x - 0.25f, bandY - 0.12f, towerFront - 0.02f),
                    Vector3(x + 0.25f, bandY + 0.12f, towerFront + reveal + 0.04f), 1.0f);
            }
            Box(frames, Vector3(-tw - 0.03f, bandY - 0.07f, towerFront - 0.02f),
                Vector3(tw + 0.03f, bandY + 0.07f, towerFront + 0.11f), 1.0f);
            const float oculusY = chapel ? 4.7f : 6.1f;
            const float oculusR = chapel ? 0.46f : 0.72f;
            frames.AddCylinder(Vector3(0.0f, oculusY, towerFront + 0.015f), front, oculusR, 0.075f, 20, true);
            dark.AddCylinder(Vector3(0.0f, oculusY, towerFront + 0.09f), front, oculusR - 0.12f, 0.018f, 20, true);
            metal.AddBox(Vector3(-0.027f, oculusY - oculusR + 0.12f, towerFront + 0.11f),
                         Vector3(0.027f, oculusY + oculusR - 0.12f, towerFront + 0.135f), 1.0f);
            metal.AddBox(Vector3(-oculusR + 0.12f, oculusY - 0.027f, towerFront + 0.11f),
                         Vector3(oculusR - 0.12f, oculusY + 0.027f, towerFront + 0.135f), 1.0f);
            if (!chapel) {
                // A restrained stone portal frames the public entrance. Its capitals and
                // pediment read from the square without covering the working door leaves.
                for (const float side : {-1.0f, 1.0f}) {
                    const float x = side * 1.38f;
                    Box(concrete, Vector3(x - 0.16f, 0.04f, towerFront + 0.01f),
                        Vector3(x + 0.16f, 3.72f, towerFront + 0.22f), 1.0f);
                    Box(concrete, Vector3(x - 0.24f, 3.48f, towerFront + 0.01f),
                        Vector3(x + 0.24f, 3.78f, towerFront + 0.28f), 1.0f);
                }
                Box(concrete, Vector3(-1.79f, 3.76f, towerFront + 0.01f),
                    Vector3(1.79f, 3.92f, towerFront + 0.30f), 1.0f);
                const Vector3 n(0.0f, 0.0f, 1.0f);
                const float z = towerFront + 0.17f;
                const auto leftCorner = concrete.AddVertex(Vector3(-1.72f, 3.92f, z), n, Vector2(0.0f, 1.0f), kWhite);
                const auto rightCorner = concrete.AddVertex(Vector3(1.72f, 3.92f, z), n, Vector2(1.0f, 1.0f), kWhite);
                const auto apex = concrete.AddVertex(Vector3(0.0f, 4.32f, z), n, Vector2(0.5f, 0.0f), kWhite);
                concrete.AddTriangle(leftCorner, rightCorner, apex);
            }
        } else {
            const unsigned seed = b.spec->seed;
            const bool hipped = type == "hall" || type == "shop" || (type == "house" && seed % 5u == 0u);
            const float ridge = b.roofHeight;
            Roof(roof, walls, frames, metal, hw, hd, h, ridge, hipped, 0.45f, 0.12f, -drop + 0.3f);
            Chimney(trim, frames, dark, Vector3(hw * 0.4f, h + ridge * 0.55f, -hd * 0.3f), 0.5f, ridge * 0.6f + 0.8f);
            if (type == "barn") {
                Door(trim, frames, metal, dark, Vector3(0.0f, 0.0f, hd), 3.6f, 3.4f, front);
                WindowRow(windows, trim, nullptr, nullptr, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, back, false);
                WindowRow(windows, trim, nullptr, nullptr, hw, hd, h * 0.55f, 0.7f, 0.9f, 4.0f, left, false);
            } else {
                for (int f = 0; f < floors; ++f) {
                    const float y = static_cast<float>(f) * floorH + (f == 0 ? 1.0f : 0.95f);
                    const bool shopFront = type == "shop" && f == 0;
                    const int doorSlot = f == 0 ? 0 : -1;
                    // Street facades of town houses and shops carry window surrounds; farm
                    // houses and the backs of buildings stay plain, as they are in the villages.
                    const bool dressed = floors >= 2 && type != "farm";
                    const bool shutterFront = (type == "house" && seed % 4u == 0u) ||
                                              (type == "cottage" && seed % 3u == 1u);
                    if (shopFront) {
                        WindowRow(glass, trim, &frames, &dark, hw, hd, 0.5f, 2.2f, 2.4f, 3.0f, front, false, 0);
                        // A shop reads as a continuous street frontage rather than a house
                        // with oversized windows. Bay piers, a fascia and a shallow ledge
                        // share the existing material batches; alternate shops have a
                        // projecting canopy, giving neighbouring square fronts different
                        // silhouettes without changing their footprints or collision.
                        const int shopBays = std::max(1, static_cast<int>((hw * 2.0f - 0.8f) / 3.0f));
                        const float baySpan = (hw * 2.0f - 0.8f) / static_cast<float>(shopBays);
                        for (int bay = 1; bay < shopBays; ++bay) {
                            const float x = -hw + 0.4f + static_cast<float>(bay) * baySpan;
                            Box(frames, Vector3(x - 0.105f, 0.46f, hd + 0.025f),
                                Vector3(x + 0.105f, 2.78f, hd + 0.115f), 1.0f);
                        }
                        Box(trim, Vector3(-hw + 0.36f, 2.78f, hd + 0.025f),
                            Vector3(hw - 0.36f, 3.19f, hd + 0.105f), 1.0f);
                        Box(frames, Vector3(-hw + 0.31f, 3.19f, hd + 0.025f),
                            Vector3(hw - 0.31f, 3.27f, hd + 0.145f), 1.0f);
                        ShopSign(frames, dark, seed, hd);
                        if (seed % 2u == 0u) {
                            Box(concrete, Vector3(-hw + 0.31f, 2.69f, hd + 0.08f),
                                Vector3(hw - 0.31f, 2.80f, hd + 0.78f), 0.5f);
                            Box(metal, Vector3(-hw + 0.31f, 2.67f, hd + 0.72f),
                                Vector3(hw - 0.31f, 2.72f, hd + 0.80f), 1.0f);
                        } else {
                            Box(frames, Vector3(-hw + 0.31f, 2.69f, hd + 0.025f),
                                Vector3(hw - 0.31f, 2.78f, hd + 0.19f), 1.0f);
                        }
                    } else {
                        WindowRow(windows, trim, &frames, &dark, hw, hd, y, 1.35f, 1.05f, 2.4f,
                                  front, true, doorSlot, dressed && !shutterFront, shutterFront);
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
                Door(trim, frames, metal, dark, Vector3(doorX, 0.0f, hd), 1.0f, 2.15f, front);
                Box(concrete, Vector3(doorX - 0.8f, -drop, hd), Vector3(doorX + 0.8f, 0.03f, hd + 0.9f), 0.5f);
                if (type != "shop" || seed % 2u != 0u) {
                    Box(frames, Vector3(doorX - 0.85f, 2.27f, hd), Vector3(doorX + 0.85f, 2.35f, hd + 0.75f), 1.0f);
                }
                // Cornice under the eaves and a string course between floors on town houses.
                if (floors >= 2) {
                    Box(frames, Vector3(-hw - 0.05f, h - 0.24f, -hd - 0.05f), Vector3(hw + 0.05f, h - 0.08f, hd + 0.05f), 1.0f);
                    Box(frames, Vector3(-hw - 0.03f, floorH - 0.04f, -hd - 0.03f), Vector3(hw + 0.03f, floorH + 0.04f, hd + 0.03f), 1.0f);
                    // Corner pilasters (lizény) from the plinth to the cornice.
                    for (const float sx : {-1.0f, 1.0f}) {
                        for (const float sz : {-1.0f, 1.0f}) {
                            const float x0 = sx * hw, z0 = sz * hd;
                            Box(frames, Vector3(std::min(x0, x0 - sx * 0.45f) - 0.02f, 0.45f, std::min(z0, z0 + sz * 0.035f)),
                                Vector3(std::max(x0, x0 - sx * 0.45f) + 0.02f, h - 0.24f, std::max(z0, z0 + sz * 0.035f)), 1.0f);
                            Box(frames, Vector3(std::min(x0, x0 + sx * 0.035f), 0.45f, std::min(z0, z0 - sz * 0.45f) - 0.02f),
                                Vector3(std::max(x0, x0 + sx * 0.035f), h - 0.24f, std::max(z0, z0 - sz * 0.45f) + 0.02f), 1.0f);
                        }
                    }
                } else if (type != "barn") {
                    // Even a cottage has a plain band under the eaves.
                    Box(frames, Vector3(-hw - 0.03f, h - 0.18f, -hd - 0.03f), Vector3(hw + 0.03f, h - 0.06f, hd + 0.03f), 1.0f);
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
