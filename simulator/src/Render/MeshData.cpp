#include "CarSim/Render/MeshData.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Render
{
    std::uint32_t MeshData::AddVertex(const MeshVertex& v)
    {
        vertices.push_back(v);
        return static_cast<std::uint32_t>(vertices.size() - 1);
    }

    std::uint32_t MeshData::AddVertex(const Vector3& position, const Vector3& normal, const Vector2& uv, const Color& color)
    {
        MeshVertex v;
        v.position = position;
        v.normal = normal;
        v.uv = uv;
        v.color = color;
        return AddVertex(v);
    }

    void MeshData::AddTriangle(const std::uint32_t a, const std::uint32_t b, const std::uint32_t c)
    {
        // Counter-clockwise from outside in, clockwise (XNA front face) out.
        indices.push_back(a);
        indices.push_back(c);
        indices.push_back(b);
    }

    void MeshData::AddQuad(const std::uint32_t a, const std::uint32_t b, const std::uint32_t c, const std::uint32_t d)
    {
        AddTriangle(a, b, c);
        AddTriangle(a, c, d);
    }

    void MeshData::AddQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& normal,
                           const Vector2& uvA, const Vector2& uvB, const Vector2& uvC, const Vector2& uvD, const Color& color)
    {
        const std::uint32_t ia = AddVertex(a, normal, uvA, color);
        const std::uint32_t ib = AddVertex(b, normal, uvB, color);
        const std::uint32_t ic = AddVertex(c, normal, uvC, color);
        const std::uint32_t id = AddVertex(d, normal, uvD, color);
        AddQuad(ia, ib, ic, id);
    }

    void MeshData::AddBox(const Vector3& mn, const Vector3& mx, const float uvScale, const Color& color)
    {
        const float s = uvScale > 0.0f ? 1.0f / uvScale : 1.0f;
        const auto face = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& n,
                              float w, float h) {
            AddQuad(a, b, c, d, n, Vector2(0.0f, h * s), Vector2(w * s, h * s), Vector2(w * s, 0.0f), Vector2(0.0f, 0.0f), color);
        };
        const float w = mx.X - mn.X;
        const float h = mx.Y - mn.Y;
        const float dpt = mx.Z - mn.Z;
        // +Z (front in model terms, faces the viewer standing at +Z)
        face(Vector3(mn.X, mn.Y, mx.Z), Vector3(mx.X, mn.Y, mx.Z), Vector3(mx.X, mx.Y, mx.Z), Vector3(mn.X, mx.Y, mx.Z), Vector3(0, 0, 1), w, h);
        // -Z
        face(Vector3(mx.X, mn.Y, mn.Z), Vector3(mn.X, mn.Y, mn.Z), Vector3(mn.X, mx.Y, mn.Z), Vector3(mx.X, mx.Y, mn.Z), Vector3(0, 0, -1), w, h);
        // +X
        face(Vector3(mx.X, mn.Y, mx.Z), Vector3(mx.X, mn.Y, mn.Z), Vector3(mx.X, mx.Y, mn.Z), Vector3(mx.X, mx.Y, mx.Z), Vector3(1, 0, 0), dpt, h);
        // -X
        face(Vector3(mn.X, mn.Y, mn.Z), Vector3(mn.X, mn.Y, mx.Z), Vector3(mn.X, mx.Y, mx.Z), Vector3(mn.X, mx.Y, mn.Z), Vector3(-1, 0, 0), dpt, h);
        // +Y
        face(Vector3(mn.X, mx.Y, mx.Z), Vector3(mx.X, mx.Y, mx.Z), Vector3(mx.X, mx.Y, mn.Z), Vector3(mn.X, mx.Y, mn.Z), Vector3(0, 1, 0), w, dpt);
        // -Y
        face(Vector3(mn.X, mn.Y, mn.Z), Vector3(mx.X, mn.Y, mn.Z), Vector3(mx.X, mn.Y, mx.Z), Vector3(mn.X, mn.Y, mx.Z), Vector3(0, -1, 0), w, dpt);
    }

    namespace
    {
        void Basis(const Vector3& axis, Vector3& u, Vector3& v)
        {
            const Vector3 helper = std::fabs(axis.Y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
            u = Vector3::Cross(helper, axis);
            u.Normalize();
            v = Vector3::Cross(axis, u);
            v.Normalize();
        }
    }

    void MeshData::AddCylinder(const Vector3& base, const Vector3& axisIn, const float radius, const float length,
                               const int segments, const bool caps, const Color& color, const float uvScale)
    {
        Vector3 axis = axisIn;
        axis.Normalize();
        Vector3 u, v;
        Basis(axis, u, v);
        const int n = std::max(3, segments);
        const std::uint32_t start = static_cast<std::uint32_t>(vertices.size());
        const float circumference = 2.0f * std::numbers::pi_v<float> * radius;
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            const float a = t * 2.0f * std::numbers::pi_v<float>;
            const Vector3 normal = u * std::cos(a) + v * std::sin(a);
            const Vector3 p0 = base + normal * radius;
            const Vector3 p1 = p0 + axis * length;
            AddVertex(p0, normal, Vector2(t * circumference / uvScale, 0.0f), color);
            AddVertex(p1, normal, Vector2(t * circumference / uvScale, length / uvScale), color);
        }
        for (int i = 0; i < n; ++i) {
            const std::uint32_t a = start + static_cast<std::uint32_t>(i * 2);
            // Outside view: p0(i) -> p0(i+1) -> p1(i+1) -> p1(i) is counter-clockwise from outside.
            AddQuad(a, a + 2, a + 3, a + 1);
        }
        if (caps) {
            const std::uint32_t c0 = AddVertex(base, -axis, Vector2(0.5f, 0.5f), color);
            const std::uint32_t c1 = AddVertex(base + axis * length, axis, Vector2(0.5f, 0.5f), color);
            for (int i = 0; i < n; ++i) {
                const float a0 = static_cast<float>(i) / static_cast<float>(n) * 2.0f * std::numbers::pi_v<float>;
                const float a1 = static_cast<float>(i + 1) / static_cast<float>(n) * 2.0f * std::numbers::pi_v<float>;
                const Vector3 r0 = u * std::cos(a0) + v * std::sin(a0);
                const Vector3 r1 = u * std::cos(a1) + v * std::sin(a1);
                const std::uint32_t b0 = AddVertex(base + r0 * radius, -axis, Vector2(0.5f + 0.5f * std::cos(a0), 0.5f + 0.5f * std::sin(a0)), color);
                const std::uint32_t b1 = AddVertex(base + r1 * radius, -axis, Vector2(0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)), color);
                AddTriangle(c0, b1, b0);
                const std::uint32_t t0 = AddVertex(base + axis * length + r0 * radius, axis, Vector2(0.5f + 0.5f * std::cos(a0), 0.5f + 0.5f * std::sin(a0)), color);
                const std::uint32_t t1 = AddVertex(base + axis * length + r1 * radius, axis, Vector2(0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)), color);
                AddTriangle(c1, t0, t1);
            }
        }
    }

    void MeshData::AddTorus(const Vector3& center, const Vector3& axisIn, const float majorRadius, const float minorRadius,
                            const int majorSegments, const int minorSegments, const Color& color)
    {
        Vector3 axis = axisIn;
        axis.Normalize();
        Vector3 u, v;
        Basis(axis, u, v);
        const int nMajor = std::max(3, majorSegments);
        const int nMinor = std::max(3, minorSegments);
        const std::uint32_t start = static_cast<std::uint32_t>(vertices.size());
        for (int i = 0; i <= nMajor; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(nMajor) * 2.0f * std::numbers::pi_v<float>;
            const Vector3 radial = u * std::cos(a) + v * std::sin(a);
            const Vector3 ringCenter = center + radial * majorRadius;
            for (int j = 0; j <= nMinor; ++j) {
                const float b = static_cast<float>(j) / static_cast<float>(nMinor) * 2.0f * std::numbers::pi_v<float>;
                const Vector3 normal = radial * std::cos(b) + axis * std::sin(b);
                AddVertex(ringCenter + normal * minorRadius, normal,
                          Vector2(static_cast<float>(i) / static_cast<float>(nMajor), static_cast<float>(j) / static_cast<float>(nMinor)), color);
            }
        }
        const auto idx = [&](int i, int j) { return start + static_cast<std::uint32_t>(i * (nMinor + 1) + j); };
        for (int i = 0; i < nMajor; ++i) {
            for (int j = 0; j < nMinor; ++j) {
                AddQuad(idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1));
            }
        }
    }

    void MeshData::AddLoft(const std::vector<std::vector<Vector3>>& rings, const bool closedRings, const Color& color,
                           const float uScale, const float vScale)
    {
        if (rings.size() < 2 || rings.front().size() < 2) {
            return;
        }
        const std::size_t ringSize = rings.front().size();
        const std::uint32_t start = static_cast<std::uint32_t>(vertices.size());
        float vAccum = 0.0f;
        for (std::size_t r = 0; r < rings.size(); ++r) {
            if (r > 0) {
                vAccum += Vector3::Distance(rings[r][0], rings[r - 1][0]);
            }
            float uAccum = 0.0f;
            for (std::size_t i = 0; i < ringSize; ++i) {
                if (i > 0) {
                    uAccum += Vector3::Distance(rings[r][i], rings[r][i - 1]);
                }
                AddVertex(rings[r][i], Vector3(0.0f, 1.0f, 0.0f), Vector2(uAccum * uScale, vAccum * vScale), color);
            }
        }
        const auto idx = [&](std::size_t r, std::size_t i) {
            return start + static_cast<std::uint32_t>(r * ringSize + (i % ringSize));
        };
        const std::size_t spans = closedRings ? ringSize : ringSize - 1;
        for (std::size_t r = 0; r + 1 < rings.size(); ++r) {
            for (std::size_t i = 0; i < spans; ++i) {
                AddQuad(idx(r, i), idx(r, i + 1), idx(r + 1, i + 1), idx(r + 1, i));
            }
        }
    }

    void MeshData::Append(const MeshData& other, const Matrix& transform)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.reserve(vertices.size() + other.vertices.size());
        for (MeshVertex v : other.vertices) {
            v.position = Vector3::Transform(v.position, transform);
            v.normal = Vector3::TransformNormal(v.normal, transform);
            if (v.normal.LengthSquared() > 1e-12f) {
                v.normal.Normalize();
            }
            vertices.push_back(v);
        }
        indices.reserve(indices.size() + other.indices.size());
        for (const std::uint32_t i : other.indices) {
            indices.push_back(base + i);
        }
    }

    void MeshData::Transform(const Matrix& transform)
    {
        for (auto& v : vertices) {
            v.position = Vector3::Transform(v.position, transform);
            v.normal = Vector3::TransformNormal(v.normal, transform);
            if (v.normal.LengthSquared() > 1e-12f) {
                v.normal.Normalize();
            }
        }
    }

    Vector3 MeshData::EmittedTriangleNormal(const std::size_t t) const
    {
        const Vector3& a = vertices[indices[t * 3]].position;
        const Vector3& b = vertices[indices[t * 3 + 1]].position;
        const Vector3& c = vertices[indices[t * 3 + 2]].position;
        return Vector3::Cross(b - a, c - a);
    }

    void MeshData::ComputeSmoothNormals()
    {
        std::vector<Vector3> sums(vertices.size(), Vector3(0.0f, 0.0f, 0.0f));
        for (std::size_t t = 0; t < TriangleCount(); ++t) {
            // Emitted order is clockwise-front, so the outward normal is minus the right-hand normal.
            const Vector3 n = -EmittedTriangleNormal(t);
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t i = indices[t * 3 + static_cast<std::size_t>(k)];
                sums[i] = sums[i] + n;
            }
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (sums[i].LengthSquared() > 1e-12f) {
                sums[i].Normalize();
                vertices[i].normal = sums[i];
            }
        }
    }

    void MeshData::MakeFlatShaded()
    {
        std::vector<MeshVertex> flat;
        std::vector<std::uint32_t> flatIndices;
        flat.reserve(indices.size());
        flatIndices.reserve(indices.size());
        for (std::size_t t = 0; t < TriangleCount(); ++t) {
            Vector3 n = -EmittedTriangleNormal(t);
            if (n.LengthSquared() > 1e-12f) {
                n.Normalize();
            }
            for (int k = 0; k < 3; ++k) {
                MeshVertex v = vertices[indices[t * 3 + static_cast<std::size_t>(k)]];
                v.normal = n;
                flat.push_back(v);
                flatIndices.push_back(static_cast<std::uint32_t>(flat.size() - 1));
            }
        }
        vertices.swap(flat);
        indices.swap(flatIndices);
    }

    void MeshData::FlipWinding()
    {
        for (std::size_t t = 0; t < TriangleCount(); ++t) {
            std::swap(indices[t * 3 + 1], indices[t * 3 + 2]);
        }
        for (auto& v : vertices) {
            v.normal = -v.normal;
        }
    }

    float MeshData::SignedVolume() const
    {
        float six = 0.0f;
        for (std::size_t t = 0; t < TriangleCount(); ++t) {
            const Vector3& a = vertices[indices[t * 3]].position;
            const Vector3& b = vertices[indices[t * 3 + 1]].position;
            const Vector3& c = vertices[indices[t * 3 + 2]].position;
            six += Vector3::Dot(a, Vector3::Cross(b, c));
        }
        return six / 6.0f;
    }

    bool MeshData::OrientOutward()
    {
        if (SignedVolume() <= 0.0f) {
            return false;
        }
        FlipWinding();
        return true;
    }

    BoundingBox MeshData::Bounds() const
    {
        if (vertices.empty()) {
            return BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
        }
        Vector3 mn = vertices.front().position;
        Vector3 mx = mn;
        for (const auto& v : vertices) {
            mn = Vector3::Min(mn, v.position);
            mx = Vector3::Max(mx, v.position);
        }
        return BoundingBox(mn, mx);
    }

    BoundingSphere MeshData::BoundingSphereOf() const
    {
        const BoundingBox box = Bounds();
        const Vector3 center = (box.Min + box.Max) * 0.5f;
        float radius = 0.0f;
        for (const auto& v : vertices) {
            radius = std::max(radius, Vector3::Distance(center, v.position));
        }
        return BoundingSphere(center, radius);
    }
}
