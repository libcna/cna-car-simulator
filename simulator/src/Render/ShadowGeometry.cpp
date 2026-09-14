#include "CarSim/Render/ShadowGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace CarSim::Render::ShadowGeometry
{
    std::vector<Vector3> ExtremePoints(const std::vector<Vector3>& points, const int directions)
    {
        std::vector<Vector3> out;
        if (points.empty() || directions <= 0) {
            return out;
        }
        const float golden = std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < directions; ++i) {
            // Fibonacci sphere: evenly spread directions.
            const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(directions);
            const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float a = golden * static_cast<float>(i);
            const Vector3 d(std::cos(a) * r, y, std::sin(a) * r);
            std::size_t best = 0;
            float bestDot = -std::numeric_limits<float>::max();
            for (std::size_t k = 0; k < points.size(); ++k) {
                const float dot = Vector3::Dot(points[k], d);
                if (dot > bestDot) {
                    bestDot = dot;
                    best = k;
                }
            }
            const Vector3& p = points[best];
            const bool seen = std::any_of(out.begin(), out.end(), [&](const Vector3& q) { return Vector3::DistanceSquared(p, q) < 1e-8f; });
            if (!seen) {
                out.push_back(p);
            }
        }
        return out;
    }

    namespace
    {
        float Cross(const Vector2& o, const Vector2& a, const Vector2& b)
        {
            return (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
        }
    }

    std::vector<Vector2> ConvexHull(std::vector<Vector2> points)
    {
        std::sort(points.begin(), points.end(), [](const Vector2& a, const Vector2& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
        points.erase(std::unique(points.begin(), points.end(), [](const Vector2& a, const Vector2& b) {
                         return std::fabs(a.X - b.X) < 1e-6f && std::fabs(a.Y - b.Y) < 1e-6f;
                     }),
                     points.end());
        const std::size_t n = points.size();
        if (n < 3) {
            return {};
        }
        std::vector<Vector2> hull(2 * n);
        std::size_t k = 0;
        for (std::size_t i = 0; i < n; ++i) {   // lower chain
            while (k >= 2 && Cross(hull[k - 2], hull[k - 1], points[i]) <= 1e-7f) --k;
            hull[k++] = points[i];
        }
        for (std::size_t i = n - 1, t = k + 1; i > 0; --i) {   // upper chain
            while (k >= t && Cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 1e-7f) --k;
            hull[k++] = points[i - 1];
        }
        hull.resize(k > 0 ? k - 1 : 0);   // the last point repeats the first
        if (hull.size() < 3) {
            return {};
        }
        return hull;
    }

    bool ProjectToPlane(const Vector3& point, const Vector3& lightDirection, const Vector3& planePoint, const Vector3& planeNormal, Vector3& out)
    {
        const float denom = Vector3::Dot(planeNormal, lightDirection);
        if (denom > -1e-4f) {
            return false;   // light parallel to the plane or shining up through it
        }
        const float t = Vector3::Dot(planeNormal, planePoint - point) / denom;
        if (t < 0.0f) {
            // The point is already below the plane: drop it straight onto the surface.
            out = point - planeNormal * Vector3::Dot(planeNormal, point - planePoint);
            return true;
        }
        out = point + lightDirection * t;
        return true;
    }

    float SignedArea(const std::vector<Vector2>& polygon)
    {
        float twice = 0.0f;
        for (std::size_t i = 0, j = polygon.empty() ? 0 : polygon.size() - 1; i < polygon.size(); j = i++) {
            twice += polygon[j].X * polygon[i].Y - polygon[i].X * polygon[j].Y;
        }
        return 0.5f * twice;
    }

    std::vector<Vector2> OutwardNormals(const std::vector<Vector2>& polygon)
    {
        const std::size_t n = polygon.size();
        std::vector<Vector2> normals(n);
        if (n < 3) {
            return normals;
        }
        const auto edgeNormal = [&](const std::size_t i) {   // outward normal of edge i -> i+1 (CCW polygon)
            const Vector2 e = polygon[(i + 1) % n] - polygon[i];
            Vector2 nrm(e.Y, -e.X);
            const float len = nrm.Length();
            return len > 1e-8f ? nrm * (1.0f / len) : Vector2(0.0f, 0.0f);
        };
        for (std::size_t i = 0; i < n; ++i) {
            const Vector2 a = edgeNormal((i + n - 1) % n);
            const Vector2 b = edgeNormal(i);
            Vector2 sum = a + b;
            const float len = sum.Length();
            if (len < 1e-5f) {
                sum = b;
            } else {
                // Scale so the offset polygon's edges stay `d` from the originals (mitre), capped
                // for sharp corners.
                const float cosHalf = std::max(0.35f, len * 0.5f);
                sum = sum * (1.0f / (len * cosHalf));
            }
            normals[i] = sum;
        }
        return normals;
    }

    void PlaneBasis(const Vector3& n, Vector3& e1, Vector3& e2)
    {
        const Vector3 helper = std::fabs(n.Y) < 0.9f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
        e1 = Vector3::Cross(helper, n);
        e1.Normalize();
        e2 = Vector3::Cross(n, e1);
        e2.Normalize();
    }
}
