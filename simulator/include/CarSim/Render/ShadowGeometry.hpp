// Stencil-free planar car shadows: the exterior is reduced to its extreme vertices once, those
// are projected along the sun onto the ground every frame and their convex hull is drawn as a
// single fan with a penumbra rim, so nothing overlaps and no stencil pass is needed (the
// silhouette of a car from a high sun is convex to within a few centimetres). Pure CPU.
#pragma once

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

namespace CarSim::Render::ShadowGeometry
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    /// The vertices of `points` that are farthest along each of `directions` directions spread
    /// evenly over the sphere (a subset of the 3D convex hull). Duplicates are removed.
    [[nodiscard]] std::vector<Vector3> ExtremePoints(const std::vector<Vector3>& points, int directions);

    /// Convex hull (Andrew's monotone chain), counter-clockwise, collinear points dropped.
    /// Fewer than three distinct points give an empty result.
    [[nodiscard]] std::vector<Vector2> ConvexHull(std::vector<Vector2> points);

    /// Where the light ray through `point` (travelling along `lightDirection`) meets the plane.
    /// Returns false when the light runs parallel to the plane or comes from below it.
    [[nodiscard]] bool ProjectToPlane(const Vector3& point, const Vector3& lightDirection, const Vector3& planePoint,
                                      const Vector3& planeNormal, Vector3& out);

    /// Signed area (positive for counter-clockwise polygons).
    [[nodiscard]] float SignedArea(const std::vector<Vector2>& polygon);

    /// Outward vertex normals of a convex counter-clockwise polygon (edge-normal bisectors,
    /// mitre-scaled so offsetting by `d * normal` keeps every edge `d` away, capped at sharp
    /// corners), used to grow a penumbra rim without self-intersection.
    [[nodiscard]] std::vector<Vector2> OutwardNormals(const std::vector<Vector2>& polygon);

    /// Orthonormal tangent basis (e1, e2) of a plane with normal `n`.
    void PlaneBasis(const Vector3& n, Vector3& e1, Vector3& e2);
}
