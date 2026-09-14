#include "CarSim/Collision/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CarSim::Collision
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    Obb Obb::FromHeading(const Vector3& centre, const Vector3& half, const float headingRad)
    {
        // Local +z points along the heading: (sin h, 0, -cos h); local +x is to its right.
        Obb b;
        b.centre = centre;
        b.half = half;
        b.axes[2] = Vector3(std::sin(headingRad), 0.0f, -std::cos(headingRad));
        b.axes[1] = Vector3(0.0f, 1.0f, 0.0f);
        b.axes[0] = Vector3(-b.axes[2].Z, 0.0f, b.axes[2].X);
        return b;
    }

    Obb Obb::FromRotation(const Vector3& centre, const Matrix& rotation, const Vector3& half)
    {
        Obb b;
        b.centre = centre;
        b.half = half;
        b.axes[0] = rotation.getRightProperty();
        b.axes[1] = rotation.getUpProperty();
        b.axes[2] = rotation.getBackwardProperty();
        for (auto& a : b.axes) {
            if (a.LengthSquared() > 1e-12f) a.Normalize();
        }
        return b;
    }

    Vector3 Obb::Corner(const int index) const
    {
        const float sx = (index & 1) ? 1.0f : -1.0f;
        const float sy = (index & 2) ? 1.0f : -1.0f;
        const float sz = (index & 4) ? 1.0f : -1.0f;
        return centre + axes[0] * (sx * half.X) + axes[1] * (sy * half.Y) + axes[2] * (sz * half.Z);
    }

    Vector3 Obb::Support(const Vector3& direction) const
    {
        Vector3 p = centre;
        const float hs[3] = {half.X, half.Y, half.Z};
        for (int i = 0; i < 3; ++i) {
            const float d = Vector3::Dot(direction, axes[static_cast<std::size_t>(i)]);
            p = p + axes[static_cast<std::size_t>(i)] * (d >= 0.0f ? hs[i] : -hs[i]);
        }
        return p;
    }

    Vector3 Obb::ToLocal(const Vector3& point) const
    {
        const Vector3 d = point - centre;
        return Vector3(Vector3::Dot(d, axes[0]), Vector3::Dot(d, axes[1]), Vector3::Dot(d, axes[2]));
    }

    Vector3 Obb::ClosestPoint(const Vector3& point) const
    {
        const Vector3 l = ToLocal(point);
        const Vector3 c(std::clamp(l.X, -half.X, half.X), std::clamp(l.Y, -half.Y, half.Y), std::clamp(l.Z, -half.Z, half.Z));
        return centre + axes[0] * c.X + axes[1] * c.Y + axes[2] * c.Z;
    }

    bool Obb::Contains(const Vector3& point, const float margin) const
    {
        const Vector3 l = ToLocal(point);
        return std::fabs(l.X) <= half.X + margin && std::fabs(l.Y) <= half.Y + margin && std::fabs(l.Z) <= half.Z + margin;
    }

    float Obb::BoundingRadius() const
    {
        return half.Length();
    }

    bool IntersectObbObb(const Obb& a, const Obb& b, Contact& out)
    {
        const Vector3 d = a.centre - b.centre;
        float bestOverlap = std::numeric_limits<float>::max();
        Vector3 bestAxis(0.0f, 1.0f, 0.0f);
        const float ha[3] = {a.half.X, a.half.Y, a.half.Z};
        const float hb[3] = {b.half.X, b.half.Y, b.half.Z};

        const auto test = [&](Vector3 axis) {
            const float len = axis.Length();
            if (len < 1e-6f) {
                return true;   // degenerate cross product: skip
            }
            axis = axis * (1.0f / len);
            float ra = 0.0f;
            float rb = 0.0f;
            for (int i = 0; i < 3; ++i) {
                ra += ha[i] * std::fabs(Vector3::Dot(axis, a.axes[static_cast<std::size_t>(i)]));
                rb += hb[i] * std::fabs(Vector3::Dot(axis, b.axes[static_cast<std::size_t>(i)]));
            }
            const float dist = Vector3::Dot(axis, d);
            const float overlap = ra + rb - std::fabs(dist);
            if (overlap < 0.0f) {
                return false;
            }
            if (overlap < bestOverlap) {
                bestOverlap = overlap;
                bestAxis = dist >= 0.0f ? axis : axis * -1.0f;
            }
            return true;
        };

        for (int i = 0; i < 3; ++i) {
            if (!test(a.axes[static_cast<std::size_t>(i)])) return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (!test(b.axes[static_cast<std::size_t>(i)])) return false;
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (!test(Vector3::Cross(a.axes[static_cast<std::size_t>(i)], b.axes[static_cast<std::size_t>(j)]))) return false;
            }
        }

        out.normal = bestAxis;
        out.penetration = bestOverlap;
        // Contact point: average of the corners of one box inside the other.
        Vector3 sum(0.0f, 0.0f, 0.0f);
        int count = 0;
        for (int i = 0; i < 8; ++i) {
            const Vector3 ca = a.Corner(i);
            if (b.Contains(ca, 0.01f)) { sum = sum + ca; ++count; }
            const Vector3 cb = b.Corner(i);
            if (a.Contains(cb, 0.01f)) { sum = sum + cb; ++count; }
        }
        if (count > 0) {
            out.point = sum * (1.0f / static_cast<float>(count));
        } else {
            // Edge-edge case: midpoint between the two support points along the normal.
            out.point = (a.Support(bestAxis * -1.0f) + b.Support(bestAxis)) * 0.5f;
        }
        return true;
    }

    bool IntersectObbCylinder(const Obb& box, const VerticalCylinder& cylinder, Contact& out)
    {
        const Vector3 p0 = cylinder.base;
        const Vector3 p1 = cylinder.base + Vector3(0.0f, cylinder.height, 0.0f);
        // Closest points between the axis segment and the box by alternating projection.
        Vector3 axisPoint = (p0 + p1) * 0.5f;
        Vector3 boxPoint = box.ClosestPoint(axisPoint);
        for (int i = 0; i < 4; ++i) {
            const float t = std::clamp((boxPoint.Y - p0.Y) / std::max(1e-4f, cylinder.height), 0.0f, 1.0f);
            axisPoint = p0 + (p1 - p0) * t;
            boxPoint = box.ClosestPoint(axisPoint);
        }
        const Vector3 diff = axisPoint - boxPoint;
        const float dist = diff.Length();
        if (dist >= cylinder.radius) {
            return false;
        }
        Vector3 n(diff.X, 0.0f, diff.Z);
        if (n.LengthSquared() < 1e-8f) {
            // The axis passes through the box: push along the box axis with the least overlap.
            const Vector3 l = box.ToLocal(axisPoint);
            const float ox = box.half.X - std::fabs(l.X);
            const float oz = box.half.Z - std::fabs(l.Z);
            if (ox < oz) {
                n = box.axes[0] * (l.X >= 0.0f ? 1.0f : -1.0f);
                out.penetration = ox + cylinder.radius;
            } else {
                n = box.axes[2] * (l.Z >= 0.0f ? 1.0f : -1.0f);
                out.penetration = oz + cylinder.radius;
            }
            n.Y = 0.0f;
            if (n.LengthSquared() > 1e-8f) n.Normalize();
            out.normal = n * -1.0f;   // from the cylinder towards the box
            out.point = boxPoint;
            return true;
        }
        n.Normalize();
        // n points from the box towards the cylinder axis; the contact normal must push the box away.
        out.normal = n * -1.0f;
        out.penetration = cylinder.radius - dist;
        out.point = boxPoint;
        return true;
    }
}
