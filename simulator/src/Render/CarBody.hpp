// Internal helpers shared by the procedural car generator files (exterior, wheels, cockpit).
// Not part of the public include tree.
#pragma once

#include "CarSim/Render/MeshData.hpp"
#include "CarSim/Render/ProceduralCar.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace CarSim::Render::CarBody
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    constexpr float kPi = std::numbers::pi_v<float>;
    inline const Color kWhite(255, 255, 255, 255);

    [[nodiscard]] inline float SmoothStep(const float a, const float b, const float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    [[nodiscard]] inline float Lerp(const float a, const float b, const float t) { return a + (b - a) * t; }

    /// Monotone piecewise-cubic curve (Fritsch-Carlson) through sorted knots; clamped outside.
    class SmoothCurve
    {
    public:
        SmoothCurve() = default;
        explicit SmoothCurve(std::vector<std::pair<float, float>> knots) { Set(std::move(knots)); }
        void Set(std::vector<std::pair<float, float>> knots);
        [[nodiscard]] float Evaluate(float x) const;
        [[nodiscard]] float Slope(float x) const;
        [[nodiscard]] bool Empty() const { return knots_.empty(); }

    private:
        std::vector<std::pair<float, float>> knots_;
        std::vector<float> tangents_;
    };

    /// Ring topology of the body loft (right half, index -> feature). The left half mirrors it.
    namespace Ring
    {
        constexpr int kUnderbodyCentre = 0;
        constexpr int kUnderbodyEdge = 1;
        constexpr int kRockerBottom = 2;
        constexpr int kRockerMid = 3;
        constexpr int kRockerTop = 4;
        constexpr int kDoorBottom = 5;
        constexpr int kDoorFirst = 6;      // 6..10 door band
        constexpr int kBelt = 11;
        constexpr int kGlassBase = 12;
        constexpr int kTumbleFirst = 13;   // 13..18
        constexpr int kRail = 19;
        constexpr int kCrownFirst = 20;    // 20..26, 26 = top centre
        constexpr int kTop = 26;
        constexpr int kHalfPoints = 27;
        constexpr int kPoints = kHalfPoints * 2 - 2;   // closed ring: 52

        /// Right-half segment index (between points seg and seg+1) for a closed-ring segment.
        [[nodiscard]] inline int RightSegment(const int ringSegment) { return ringSegment <= kTop - 1 ? ringSegment : kPoints - 1 - ringSegment; }
        /// Right-half point index for a closed-ring point.
        [[nodiscard]] inline int RightPoint(const int ringPoint) { return ringPoint <= kTop ? ringPoint : kPoints - ringPoint; }
        [[nodiscard]] inline bool IsLeft(const int ringPoint) { return ringPoint > kTop; }
    }

    /// The body skin as a grid of rings (stations along z, points around) with per-vertex UVs.
    struct SkinGrid
    {
        std::vector<float> stations;                 // z per ring
        std::vector<std::vector<Vector3>> rings;     // [station][point], Ring::kPoints each
        std::vector<std::vector<Vector3>> normals;   // smooth normals, same layout
        std::vector<float> u;                        // u per ring point (fixed)
        float vScale = 1.0f;                         // v = (z - frontZ) * vScale
        float frontZ = 0.0f;

        [[nodiscard]] float V(float z) const { return (z - frontZ) * vScale; }
        [[nodiscard]] float ZOfV(float v) const { return frontZ + v / vScale; }
        /// Fractional ring index of a u value (wraps).
        [[nodiscard]] float PointOfU(float uu) const;
        /// Surface position/normal at (u, v) by bilinear interpolation of the grid.
        void Sample(float uu, float v, Vector3& position, Vector3& normal) const;
        void ComputeNormals();
    };

    /// Point-in-polygon in 2D (even-odd).
    [[nodiscard]] bool InsidePolygon(const Vector2& p, const std::vector<Vector2>& polygon);

    /// Revolves a (radius, axial) profile about `axis` through `centre`. `uRepeat` tiles the
    /// texture around the circumference; v runs along the profile.
    void AddRevolve(MeshData& mesh, const std::vector<Vector2>& profile, const Vector3& centre, const Vector3& axis, int segments,
                    float uRepeat, bool smooth = true);

    /// Zipper triangulation between two closed loops (outward orientation given by `flip`).
    void AddZipper(MeshData& mesh, const std::vector<std::uint32_t>& loopA, const std::vector<std::uint32_t>& loopB, bool flip);

    /// Box with rounded vertical edges, local axes, appended with `transform`.
    void AddRoundedBox(MeshData& mesh, const Vector3& size, float radius, const Matrix& transform, float uvScale = 1.0f);

    /// Ellipsoid (radii rx, ry, rz) at `centre`.
    void AddEllipsoid(MeshData& mesh, const Vector3& centre, const Vector3& radii, int rings, int segments);

    CarPart MakePart(const std::string& name, CarMaterial material, CarPart::Role role = CarPart::Role::Static);
    void AddBoxTo(CarPart& part, const Vector3& centre, const Vector3& size);
    void AddOrientedBox(CarPart& part, const Vector3& centre, const Vector3& size, float yaw, float pitch, float roll = 0.0f);

    /// Wheel meshes (tyre, rim, disc) in the wheel frame (axis +X points outwards on the right side).
    void BuildWheel(const CarStyle& style, float tyreHalfWidth, MeshData& tyre, MeshData& rim, MeshData& disc);

    /// Cockpit parts appended to the model (needs the skin for the inner shell).
    void BuildCockpit(CarModel& model, const CarStyle& style, const Sim::VehicleDefinition* definition, const SkinGrid& skin,
                      const std::vector<std::vector<CarMaterial>>& skinMaterials, float zCowl, float zRoofFront, float zSideGlassRear);
}
