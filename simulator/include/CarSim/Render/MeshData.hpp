// CPU-side mesh construction with the XNA vertex layouts the renderers accept.
//
// Winding convention: XNA/CNA front faces are clockwise as seen by the viewer
// (RasterizerState::CullCounterClockwise culls counter-clockwise triangles). The builder API
// takes triangles in the natural counter-clockwise-from-outside order and emits them in the
// order the renderer expects, so callers never think about it.
#pragma once

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <vector>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    /// Renderer-independent vertex used while building; converted to the GPU layout on upload.
    struct MeshVertex
    {
        Vector3 position{};
        Vector3 normal{0.0f, 1.0f, 0.0f};
        Vector2 uv{};
        Vector2 uv2{};             // second set (lightmaps); ignored by single-UV layouts
        Color color{255, 255, 255, 255};
    };

    /// Index-based triangle mesh with helpers for procedural geometry.
    class MeshData
    {
    public:
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;

        [[nodiscard]] bool Empty() const { return indices.empty(); }
        [[nodiscard]] std::size_t TriangleCount() const { return indices.size() / 3; }

        std::uint32_t AddVertex(const MeshVertex& v);
        std::uint32_t AddVertex(const Vector3& position, const Vector3& normal, const Vector2& uv,
                                const Color& color = Color(255, 255, 255, 255));

        /// Adds a triangle given counter-clockwise from the outside (right-hand normal points out).
        void AddTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);
        /// Adds a quad a-b-c-d given counter-clockwise from the outside.
        void AddQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);

        /// Convenience: a flat quad with its own four vertices and the given normal.
        void AddQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                     const Vector3& normal, const Vector2& uvA, const Vector2& uvB, const Vector2& uvC,
                     const Vector2& uvD, const Color& color = Color(255, 255, 255, 255));

        /// Axis-aligned box from min to max with per-face UVs tiled by `uvScale` (metres per tile).
        void AddBox(const Vector3& min, const Vector3& max, float uvScale = 1.0f,
                    const Color& color = Color(255, 255, 255, 255));

        /// Cylinder along `axis` (unit) from `base`, radius, length, segments; optional caps.
        void AddCylinder(const Vector3& base, const Vector3& axis, float radius, float length, int segments,
                         bool caps, const Color& color = Color(255, 255, 255, 255), float uvScale = 1.0f);

        /// Torus centred at `center` with the ring in the plane perpendicular to `axis`.
        void AddTorus(const Vector3& center, const Vector3& axis, float majorRadius, float minorRadius,
                      int majorSegments, int minorSegments, const Color& color = Color(255, 255, 255, 255));

        /// Loft: connects consecutive closed or open rings of equal vertex count. `closedRings`
        /// closes each ring on itself. Normals are computed afterwards.
        void AddLoft(const std::vector<std::vector<Vector3>>& rings, bool closedRings,
                     const Color& color = Color(255, 255, 255, 255), float uScale = 1.0f, float vScale = 1.0f);

        /// Appends another mesh transformed by `transform`.
        void Append(const MeshData& other, const Matrix& transform);
        void Transform(const Matrix& transform);

        /// Recomputes smooth vertex normals from the face normals (area weighted).
        void ComputeSmoothNormals();
        /// Duplicates vertices per triangle and assigns flat normals.
        void MakeFlatShaded();
        /// Flips the facing of every triangle.
        void FlipWinding();
        /// Signed volume enclosed by a closed mesh under the right-hand rule of the emitted
        /// index order: negative when the mesh is wound front-facing from outside (XNA clockwise
        /// front faces), positive when it is inside out.
        [[nodiscard]] float SignedVolume() const;
        /// Flips a closed mesh that is inside out (positive signed volume) so its front faces
        /// and smooth normals point outward. Returns true when it flipped.
        bool OrientOutward();

        [[nodiscard]] BoundingBox Bounds() const;
        [[nodiscard]] BoundingSphere BoundingSphereOf() const;

        /// Right-hand-rule geometric normal of the emitted triangle `t` (for tests/tools).
        [[nodiscard]] Vector3 EmittedTriangleNormal(std::size_t t) const;
    };
}
