// Stencil-free car shadows: hull, projection and extreme-point decimation, plus the Lipan's
// silhouette under the fixed sun. Regression cover for the shadow that vanished after the
// first frames when it depended on a per-frame stencil clear.
#include "CarSim/Render/LightingRig.hpp"
#include "CarSim/Render/ProceduralCar.hpp"
#include "CarSim/Render/ShadowGeometry.hpp"
#include "CarSim/Sim/VehicleDefinition.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace CarSim;
using namespace CarSim::Render;
using namespace CarSim::Render::ShadowGeometry;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    bool Inside(const std::vector<Vector2>& hull, const Vector2& p, const float slack)
    {
        // Convex CCW polygon: the point is inside when it lies left of (or within slack of) every edge.
        for (std::size_t i = 0; i < hull.size(); ++i) {
            const Vector2& a = hull[i];
            const Vector2& b = hull[(i + 1) % hull.size()];
            const Vector2 e = b - a;
            const float len = e.Length();
            if (len < 1e-6f) continue;
            const float cross = (e.X * (p.Y - a.Y) - e.Y * (p.X - a.X)) / len;
            if (cross < -slack) return false;
        }
        return true;
    }
}

TEST(ShadowGeometry, ConvexHullDropsInteriorAndCollinearPoints)
{
    std::vector<Vector2> pts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5f, 0.5f}, {0.5f, 0.0f}, {0.2f, 0.7f}, {1, 1}};
    const auto hull = ConvexHull(pts);
    ASSERT_EQ(hull.size(), 4u);
    EXPECT_NEAR(SignedArea(hull), 1.0f, 1e-5f);   // counter-clockwise
    for (const auto& p : pts) EXPECT_TRUE(Inside(hull, p, 1e-5f));
    EXPECT_TRUE(ConvexHull({{0, 0}, {1, 1}}).empty());
    EXPECT_TRUE(ConvexHull({{0, 0}, {1, 1}, {2, 2}}).empty());   // collinear
}

TEST(ShadowGeometry, ExtremePointsKeepTheBoundingBoxOfACloud)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<Vector3> cloud;
    for (int i = 0; i < 4000; ++i) cloud.emplace_back(u(rng) * 2.0f, u(rng) * 0.7f, u(rng) * 1.3f);
    // A dense cloud: the sampled directions reach within a few percent of the true extent.
    {
        const auto extreme = ExtremePoints(cloud, 96);
        ASSERT_FALSE(extreme.empty());
        EXPECT_LE(extreme.size(), 96u);
        Vector3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f), elo = lo, ehi = hi;
        for (const auto& p : cloud) { lo = Vector3::Min(lo, p); hi = Vector3::Max(hi, p); }
        for (const auto& p : extreme) { elo = Vector3::Min(elo, p); ehi = Vector3::Max(ehi, p); }
        EXPECT_NEAR(elo.X, lo.X, 0.12f); EXPECT_NEAR(ehi.X, hi.X, 0.12f);
        EXPECT_NEAR(elo.Y, lo.Y, 0.06f); EXPECT_NEAR(ehi.Y, hi.Y, 0.06f);
        EXPECT_NEAR(elo.Z, lo.Z, 0.10f); EXPECT_NEAR(ehi.Z, hi.Z, 0.10f);
    }
    // With the box corners present they are the support points of every direction: exact.
    for (const float x : {-2.0f, 2.0f}) for (const float y : {-0.7f, 0.7f}) for (const float z : {-1.3f, 1.3f}) cloud.emplace_back(x, y, z);
    const auto extreme = ExtremePoints(cloud, 96);
    EXPECT_EQ(extreme.size(), 8u);
    for (const auto& p : extreme) {
        EXPECT_NEAR(std::fabs(p.X), 2.0f, 1e-6f);
        EXPECT_NEAR(std::fabs(p.Y), 0.7f, 1e-6f);
        EXPECT_NEAR(std::fabs(p.Z), 1.3f, 1e-6f);
    }
}

TEST(ShadowGeometry, ProjectionFollowsTheLightOntoThePlane)
{
    Vector3 light(1.0f, -1.0f, 0.0f);
    light.Normalize();
    Vector3 q;
    ASSERT_TRUE(ProjectToPlane(Vector3(0.0f, 2.0f, 5.0f), light, Vector3(0, 0, 0), Vector3(0, 1, 0), q));
    EXPECT_NEAR(q.X, 2.0f, 1e-5f);   // 45 degrees: offset equals the height
    EXPECT_NEAR(q.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(q.Z, 5.0f, 1e-5f);
    // Light from below or parallel: no shadow.
    EXPECT_FALSE(ProjectToPlane(Vector3(0, 2, 0), Vector3(0, 1, 0), Vector3(0, 0, 0), Vector3(0, 1, 0), q));
    EXPECT_FALSE(ProjectToPlane(Vector3(0, 2, 0), Vector3(1, 0, 0), Vector3(0, 0, 0), Vector3(0, 1, 0), q));
    // A point below the plane drops straight onto it.
    ASSERT_TRUE(ProjectToPlane(Vector3(3.0f, -0.5f, 1.0f), light, Vector3(0, 0, 0), Vector3(0, 1, 0), q));
    EXPECT_NEAR(q.X, 3.0f, 1e-5f);
    EXPECT_NEAR(q.Y, 0.0f, 1e-5f);
}

TEST(ShadowGeometry, OutwardNormalsGrowASquareWithoutFolding)
{
    const std::vector<Vector2> square = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const auto normals = OutwardNormals(square);
    ASSERT_EQ(normals.size(), 4u);
    std::vector<Vector2> grown;
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_GT(Vector2::Dot(normals[i], square[i]), 0.0f);   // points away from the centre
        grown.push_back(square[i] + normals[i] * 0.1f);
    }
    // Mitre: every edge of the grown square is 0.1 further out.
    EXPECT_NEAR(grown[0].X, -1.1f, 1e-5f);
    EXPECT_NEAR(grown[0].Y, -1.1f, 1e-5f);
    EXPECT_NEAR(grown[2].X, 1.1f, 1e-5f);
    EXPECT_NEAR(SignedArea(grown), 2.2f * 2.2f, 1e-4f);
}

TEST(ShadowGeometry, LipanSilhouetteUnderTheFixedSunIsACarSizedHullOffsetAwayFromTheSun)
{
    const Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    const CarModel model = GenerateCar(def);
    std::vector<Vector3> exterior;
    for (const auto& part : model.parts) {
        const bool interior = part.role == CarPart::Role::Interior || part.role == CarPart::Role::SteeringWheel ||
                              part.role == CarPart::Role::GearLever || part.role == CarPart::Role::NeedleSpeed ||
                              part.role == CarPart::Role::NeedleRpm || part.role == CarPart::Role::NeedleFuel ||
                              part.role == CarPart::Role::NeedleTemp;
        if (interior || part.detail) continue;
        for (const auto& v : part.mesh.vertices) exterior.push_back(v.position);
    }
    ASSERT_GT(exterior.size(), 1000u);
    const auto casters = ExtremePoints(exterior, 160);
    EXPECT_LE(casters.size(), 160u);
    EXPECT_GE(casters.size(), 40u);

    const LightingRig rig;
    const Vector3 n(0, 1, 0);
    Vector3 e1, e2;
    PlaneBasis(n, e1, e2);
    const auto project = [&](const std::vector<Vector3>& pts) {
        std::vector<Vector2> flat;
        for (const auto& p : pts) {
            Vector3 q;
            if (ProjectToPlane(p, rig.sunDirection, Vector3(0, 0, 0), n, q)) flat.emplace_back(Vector3::Dot(q, e1), Vector3::Dot(q, e2));
        }
        return flat;
    };
    const auto hull = ConvexHull(project(casters));
    ASSERT_GE(hull.size(), 8u);
    const float area = SignedArea(hull);
    EXPECT_GT(area, 6.0f) << "a 4 m hatchback under a 48 degree sun shades at least its footprint";
    EXPECT_LT(area, 14.0f);

    // The decimated hull contains the projection of every exterior vertex: chords between the
    // sampled extreme points cut gently curved panels by a few centimetres at most.
    const auto all = project(exterior);
    std::size_t outside = 0;
    for (const auto& p : all) {
        if (!Inside(hull, p, 0.05f)) ++outside;
    }
    EXPECT_EQ(outside, 0u);

    // The hull's centroid lies away from the sun (along the light direction on the ground).
    Vector2 c(0, 0);
    for (const auto& h : hull) c = c + h;
    c = c * (1.0f / static_cast<float>(hull.size()));
    const Vector3 centroid = e1 * c.X + e2 * c.Y;
    Vector2 lightXZ(rig.sunDirection.X, rig.sunDirection.Z);
    lightXZ.Normalize();
    EXPECT_GT(centroid.X * lightXZ.X + centroid.Z * lightXZ.Y, 0.3f);
}
