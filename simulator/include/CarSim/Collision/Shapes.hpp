// Collision shapes and narrow-phase tests: oriented boxes (vehicles, buildings, furniture)
// and vertical cylinders (trees, posts). Project-owned; only XNA math types are used.
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>

namespace CarSim::Collision
{
    struct Contact
    {
        Microsoft::Xna::Framework::Vector3 point{};        // world-space contact point
        Microsoft::Xna::Framework::Vector3 normal{0, 1, 0};   // unit, points from the second shape towards the first
        float penetration = 0.0f;                          // overlap depth along the normal (>= 0)
    };

    struct Obb
    {
        Microsoft::Xna::Framework::Vector3 centre{};
        std::array<Microsoft::Xna::Framework::Vector3, 3> axes{};   // unit local x, y, z in world space
        Microsoft::Xna::Framework::Vector3 half{1.0f, 1.0f, 1.0f};

        /// Box rotated about +Y so that its local +z points along the map heading (0 = north).
        [[nodiscard]] static Obb FromHeading(const Microsoft::Xna::Framework::Vector3& centre, const Microsoft::Xna::Framework::Vector3& half,
                                             float headingRad);
        /// Box with the axes of a rotation matrix (Right, Up, Backward).
        [[nodiscard]] static Obb FromRotation(const Microsoft::Xna::Framework::Vector3& centre, const Microsoft::Xna::Framework::Matrix& rotation,
                                              const Microsoft::Xna::Framework::Vector3& half);

        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Corner(int index) const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Support(const Microsoft::Xna::Framework::Vector3& direction) const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 ClosestPoint(const Microsoft::Xna::Framework::Vector3& point) const;
        [[nodiscard]] bool Contains(const Microsoft::Xna::Framework::Vector3& point, float margin = 0.0f) const;
        [[nodiscard]] float BoundingRadius() const;
        /// Local coordinates of a world point.
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 ToLocal(const Microsoft::Xna::Framework::Vector3& point) const;
    };

    struct VerticalCylinder
    {
        Microsoft::Xna::Framework::Vector3 base{};   // bottom centre
        float radius = 0.2f;
        float height = 4.0f;
    };

    /// Separating-axis test; on overlap fills `out` with the minimum-translation normal (from `b`
    /// towards `a`), the penetration and a representative contact point.
    [[nodiscard]] bool IntersectObbObb(const Obb& a, const Obb& b, Contact& out);

    /// Box against a vertical cylinder; the normal pushes the box away from the cylinder.
    [[nodiscard]] bool IntersectObbCylinder(const Obb& box, const VerticalCylinder& cylinder, Contact& out);
}
