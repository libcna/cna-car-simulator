// Shared procedural car geometry and skin helpers. Extracted from ProceduralCar.cpp;
// their algorithms and local coordinate conventions are unchanged.
#include "CarBody.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace CarBody
    {
        void SmoothCurve::Set(std::vector<std::pair<float, float>> knots)
        {
            std::sort(knots.begin(), knots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            knots_ = std::move(knots);
            const std::size_t n = knots_.size();
            tangents_.assign(n, 0.0f);
            if (n < 2) {
                return;
            }
            std::vector<float> d(n - 1);
            for (std::size_t k = 0; k + 1 < n; ++k) {
                const float h = std::max(1e-5f, knots_[k + 1].first - knots_[k].first);
                d[k] = (knots_[k + 1].second - knots_[k].second) / h;
            }
            tangents_[0] = d[0];
            tangents_[n - 1] = d[n - 2];
            for (std::size_t k = 1; k + 1 < n; ++k) {
                tangents_[k] = (d[k - 1] * d[k] > 0.0f) ? 0.5f * (d[k - 1] + d[k]) : 0.0f;
            }
            for (std::size_t k = 0; k + 1 < n; ++k) {
                if (std::fabs(d[k]) < 1e-7f) {
                    tangents_[k] = 0.0f;
                    tangents_[k + 1] = 0.0f;
                    continue;
                }
                const float a = tangents_[k] / d[k];
                const float b = tangents_[k + 1] / d[k];
                const float s = a * a + b * b;
                if (s > 9.0f) {
                    const float tau = 3.0f / std::sqrt(s);
                    tangents_[k] = tau * a * d[k];
                    tangents_[k + 1] = tau * b * d[k];
                }
            }
        }

        float SmoothCurve::Evaluate(const float x) const
        {
            if (knots_.empty()) return 0.0f;
            if (knots_.size() == 1 || x <= knots_.front().first) return knots_.front().second;
            if (x >= knots_.back().first) return knots_.back().second;
            std::size_t k = 0;
            while (k + 2 < knots_.size() && knots_[k + 1].first <= x) ++k;
            const float x0 = knots_[k].first, x1 = knots_[k + 1].first;
            const float y0 = knots_[k].second, y1 = knots_[k + 1].second;
            const float h = std::max(1e-5f, x1 - x0);
            const float t = (x - x0) / h;
            const float t2 = t * t, t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            return h00 * y0 + h10 * h * tangents_[k] + h01 * y1 + h11 * h * tangents_[k + 1];
        }

        float SmoothCurve::Slope(const float x) const
        {
            const float e = 0.01f;
            return (Evaluate(x + e) - Evaluate(x - e)) / (2.0f * e);
        }

        float SkinGrid::PointOfU(float uu) const
        {
            uu = uu - std::floor(uu);
            const int n = static_cast<int>(u.size());
            for (int i = 0; i < n; ++i) {
                const float u0 = u[static_cast<std::size_t>(i)];
                const float u1 = i + 1 < n ? u[static_cast<std::size_t>(i + 1)] : 1.0f;
                if (uu >= u0 && uu < u1) {
                    return static_cast<float>(i) + (uu - u0) / std::max(1e-6f, u1 - u0);
                }
            }
            return 0.0f;
        }

        void SkinGrid::Sample(const float uu, const float v, Vector3& position, Vector3& normal) const
        {
            const float z = ZOfV(std::clamp(v, 0.0f, 1.0f));
            const int last = static_cast<int>(stations.size()) - 1;
            int r = 0;
            while (r + 1 < last && stations[static_cast<std::size_t>(r + 1)] <= z) ++r;
            const float z0 = stations[static_cast<std::size_t>(r)];
            const float z1 = stations[static_cast<std::size_t>(r + 1)];
            const float fr = std::clamp((z - z0) / std::max(1e-6f, z1 - z0), 0.0f, 1.0f);
            const float pf = PointOfU(uu);
            const int n = static_cast<int>(u.size());
            const int i0 = static_cast<int>(std::floor(pf)) % n;
            const int i1 = (i0 + 1) % n;
            const float fu = pf - std::floor(pf);
            const auto at = [&](const std::vector<std::vector<Vector3>>& grid, int rr) {
                const auto& ring = grid[static_cast<std::size_t>(rr)];
                return Vector3::Lerp(ring[static_cast<std::size_t>(i0)], ring[static_cast<std::size_t>(i1)], fu);
            };
            position = Vector3::Lerp(at(rings, r), at(rings, r + 1), fr);
            normal = Vector3::Lerp(at(normals, r), at(normals, r + 1), fr);
            if (normal.LengthSquared() > 1e-10f) normal.Normalize();
        }

        void SkinGrid::ComputeNormals()
        {
            const int nr = static_cast<int>(rings.size());
            const int n = Ring::kPoints;
            normals.assign(rings.size(), std::vector<Vector3>(static_cast<std::size_t>(n), Vector3(0, 1, 0)));
            for (int r = 0; r < nr; ++r) {
                const int rp = std::max(0, r - 1);
                const int rn = std::min(nr - 1, r + 1);
                for (int i = 0; i < n; ++i) {
                    const int ip = (i + n - 1) % n;
                    const int in = (i + 1) % n;
                    const Vector3 tu = rings[static_cast<std::size_t>(r)][static_cast<std::size_t>(in)] - rings[static_cast<std::size_t>(r)][static_cast<std::size_t>(ip)];
                    const Vector3 tv = rings[static_cast<std::size_t>(rn)][static_cast<std::size_t>(i)] - rings[static_cast<std::size_t>(rp)][static_cast<std::size_t>(i)];
                    Vector3 nrm = Vector3::Cross(tu, tv);
                    if (nrm.LengthSquared() < 1e-12f) {
                        nrm = r < nr / 2 ? Vector3(0, 0, -1) : Vector3(0, 0, 1);
                    }
                    nrm.Normalize();
                    normals[static_cast<std::size_t>(r)][static_cast<std::size_t>(i)] = nrm;
                }
            }
        }

        bool InsidePolygon(const Vector2& p, const std::vector<Vector2>& polygon)
        {
            bool inside = false;
            for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
                const Vector2& a = polygon[i];
                const Vector2& b = polygon[j];
                if ((a.Y > p.Y) != (b.Y > p.Y)) {
                    const float x = a.X + (p.Y - a.Y) / (b.Y - a.Y) * (b.X - a.X);
                    if (p.X < x) inside = !inside;
                }
            }
            return inside;
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

        void AddRevolve(MeshData& mesh, const std::vector<Vector2>& profile, const Vector3& centre, const Vector3& axisIn, const int segments,
                        const float uRepeat, const bool smooth)
        {
            if (profile.size() < 2) return;
            Vector3 axis = axisIn;
            axis.Normalize();
            Vector3 bu, bv;
            Basis(axis, bu, bv);
            const int n = std::max(3, segments);
            std::vector<float> s(profile.size(), 0.0f);
            for (std::size_t k = 1; k < profile.size(); ++k) {
                s[k] = s[k - 1] + Vector2::Distance(profile[k], profile[k - 1]);
            }
            const float total = std::max(1e-6f, s.back());
            MeshData local;
            for (std::size_t k = 0; k < profile.size(); ++k) {
                for (int i = 0; i <= n; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(n);
                    const float a = t * 2.0f * kPi;
                    const Vector3 radial = bu * std::cos(a) + bv * std::sin(a);
                    const Vector3 p = centre + axis * profile[k].Y + radial * profile[k].X;
                    local.AddVertex(p, radial, Vector2(t * uRepeat, s[k] / total), kWhite);
                }
            }
            const auto idx = [&](std::size_t k, int i) { return static_cast<std::uint32_t>(k * static_cast<std::size_t>(n + 1) + static_cast<std::size_t>(i)); };
            for (std::size_t k = 0; k + 1 < profile.size(); ++k) {
                for (int i = 0; i < n; ++i) {
                    local.AddQuad(idx(k, i), idx(k, i + 1), idx(k + 1, i + 1), idx(k + 1, i));
                }
            }
            if (smooth) {
                local.ComputeSmoothNormals();
            } else {
                local.MakeFlatShaded();
            }
            mesh.Append(local, Matrix::getIdentityProperty());
        }

        void AddZipper(MeshData& mesh, const std::vector<std::uint32_t>& loopA, const std::vector<std::uint32_t>& loopB, const bool flip)
        {
            const std::size_t na = loopA.size();
            const std::size_t nb = loopB.size();
            if (na < 2 || nb < 2) return;
            std::size_t i = 0, j = 0;
            const auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                if (flip) mesh.AddTriangle(a, c, b); else mesh.AddTriangle(a, b, c);
            };
            while (i < na || j < nb) {
                const float ta = static_cast<float>(i + 1) / static_cast<float>(na);
                const float tb = static_cast<float>(j + 1) / static_cast<float>(nb);
                if (j >= nb || (i < na && ta <= tb)) {
                    tri(loopA[i % na], loopA[(i + 1) % na], loopB[j % nb]);
                    ++i;
                } else {
                    tri(loopA[i % na], loopB[(j + 1) % nb], loopB[j % nb]);
                    ++j;
                }
            }
        }

        void AddRoundedBox(MeshData& mesh, const Vector3& size, const float radius, const Matrix& transform, const float uvScale)
        {
            const float hx = size.X * 0.5f, hy = size.Y * 0.5f, hz = size.Z * 0.5f;
            const float r = std::min(radius, std::min(hx, hz) * 0.95f);
            std::vector<Vector2> outline;   // (x, z) counter-clockwise seen from +y
            const int arc = 4;
            const float cx[4] = {hx - r, -(hx - r), -(hx - r), hx - r};
            const float cz[4] = {hz - r, hz - r, -(hz - r), -(hz - r)};
            for (int c = 0; c < 4; ++c) {
                for (int k = 0; k <= arc; ++k) {
                    const float a = (static_cast<float>(c) + static_cast<float>(k) / static_cast<float>(arc)) * kPi * 0.5f;
                    outline.emplace_back(cx[c] + std::cos(a) * r, cz[c] + std::sin(a) * r);
                }
            }
            MeshData local;
            const std::size_t n = outline.size();
            std::vector<std::uint32_t> bottom, top;
            for (std::size_t i = 0; i < n; ++i) {
                const Vector2& o = outline[i];
                Vector3 nrm(o.X, 0.0f, o.Y);
                nrm.Normalize();
                const float uu = static_cast<float>(i) / static_cast<float>(n) * (2.0f * (hx + hz)) / uvScale;
                bottom.push_back(local.AddVertex(Vector3(o.X, -hy, o.Y), nrm, Vector2(uu, size.Y / uvScale), kWhite));
                top.push_back(local.AddVertex(Vector3(o.X, hy, o.Y), nrm, Vector2(uu, 0.0f), kWhite));
            }
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t k = (i + 1) % n;
                // Outline is counter-clockwise from +y: bottom(i) -> bottom(k) -> top(k) -> top(i) is CCW from outside.
                local.AddQuad(bottom[i], bottom[k], top[k], top[i]);
            }
            const std::uint32_t ct = local.AddVertex(Vector3(0, hy, 0), Vector3(0, 1, 0), Vector2(0.5f, 0.5f), kWhite);
            const std::uint32_t cb = local.AddVertex(Vector3(0, -hy, 0), Vector3(0, -1, 0), Vector2(0.5f, 0.5f), kWhite);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t k = (i + 1) % n;
                const Vector2& a = outline[i];
                const Vector2& b = outline[k];
                const std::uint32_t ta = local.AddVertex(Vector3(a.X, hy, a.Y), Vector3(0, 1, 0), Vector2(a.X / uvScale, a.Y / uvScale), kWhite);
                const std::uint32_t tb = local.AddVertex(Vector3(b.X, hy, b.Y), Vector3(0, 1, 0), Vector2(b.X / uvScale, b.Y / uvScale), kWhite);
                // Seen from +y the outline runs counter-clockwise: centre -> b -> a keeps the top face outward.
                local.AddTriangle(ct, tb, ta);
                const std::uint32_t ba = local.AddVertex(Vector3(a.X, -hy, a.Y), Vector3(0, -1, 0), Vector2(a.X / uvScale, a.Y / uvScale), kWhite);
                const std::uint32_t bb = local.AddVertex(Vector3(b.X, -hy, b.Y), Vector3(0, -1, 0), Vector2(b.X / uvScale, b.Y / uvScale), kWhite);
                local.AddTriangle(cb, ba, bb);
            }
            mesh.Append(local, transform);
        }

        void AddEllipsoid(MeshData& mesh, const Vector3& centre, const Vector3& radii, const int rings, const int segments)
        {
            MeshData local;
            const int nr = std::max(2, rings);
            const int ns = std::max(3, segments);
            for (int r = 0; r <= nr; ++r) {
                const float phi = (static_cast<float>(r) / static_cast<float>(nr) - 0.5f) * kPi;   // -pi/2..pi/2
                for (int s = 0; s <= ns; ++s) {
                    const float th = static_cast<float>(s) / static_cast<float>(ns) * 2.0f * kPi;
                    const Vector3 n(std::cos(phi) * std::cos(th), std::sin(phi), std::cos(phi) * std::sin(th));
                    Vector3 nrm(n.X / radii.X, n.Y / radii.Y, n.Z / radii.Z);
                    nrm.Normalize();
                    local.AddVertex(centre + Vector3(n.X * radii.X, n.Y * radii.Y, n.Z * radii.Z), nrm,
                                    Vector2(static_cast<float>(s) / static_cast<float>(ns), static_cast<float>(r) / static_cast<float>(nr)), kWhite);
                }
            }
            const auto idx = [&](int r, int s) { return static_cast<std::uint32_t>(r * (ns + 1) + s); };
            for (int r = 0; r < nr; ++r) {
                for (int s = 0; s < ns; ++s) {
                    // theta increases towards +z at theta = 90deg... orientation: cross(d/dtheta, d/dphi) must point outwards.
                    local.AddQuad(idx(r, s), idx(r + 1, s), idx(r + 1, s + 1), idx(r, s + 1));
                }
            }
            // Verify orientation with one triangle and flip if needed.
            if (local.TriangleCount() > 0) {
                const std::size_t t = local.TriangleCount() / 2;
                const Vector3 emitted = -local.EmittedTriangleNormal(t);
                const Vector3 authored = local.vertices[local.indices[t * 3]].normal;
                if (Vector3::Dot(emitted, authored) < 0.0f) {
                    for (std::size_t k = 0; k < local.TriangleCount(); ++k) std::swap(local.indices[k * 3 + 1], local.indices[k * 3 + 2]);
                }
            }
            mesh.Append(local, Matrix::getIdentityProperty());
        }

        CarPart MakePart(const std::string& name, const CarMaterial material, const CarPart::Role role)
        {
            CarPart part;
            part.name = name;
            part.material = material;
            part.role = role;
            return part;
        }

        void AddBoxTo(CarPart& part, const Vector3& centre, const Vector3& size)
        {
            part.mesh.AddBox(centre - size * 0.5f, centre + size * 0.5f, 1.0f);
        }

        void AddOrientedBox(CarPart& part, const Vector3& centre, const Vector3& size, const float yaw, const float pitch, const float roll)
        {
            MeshData box;
            box.AddBox(size * -0.5f, size * 0.5f, 1.0f);
            box.Transform(Matrix::CreateRotationZ(roll) * Matrix::CreateRotationX(pitch) * Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(centre));
            part.mesh.Append(box, Matrix::getIdentityProperty());
        }
    }
}
