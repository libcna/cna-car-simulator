#include "CarSim/Render/MeshData.hpp"

#include <gtest/gtest.h>

using namespace CarSim::Render;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    // XNA front faces are clockwise as seen from outside: the right-hand-rule normal of the
    // emitted index order must point INWARD (against the authored outward normal).
    void ExpectXnaFrontFacing(const MeshData& mesh)
    {
        for (std::size_t t = 0; t < mesh.TriangleCount(); ++t) {
            const Vector3 emitted = mesh.EmittedTriangleNormal(t);
            const Vector3 authored = mesh.vertices[mesh.indices[t * 3]].normal;
            EXPECT_LT(Vector3::Dot(emitted, authored), 0.0f) << "triangle " << t << " is wound for the wrong side";
        }
    }
}

TEST(MeshData, BoxFacesAreFrontFacingFromOutside)
{
    MeshData mesh;
    mesh.AddBox(Vector3(-1.0f, 0.0f, -2.0f), Vector3(1.0f, 1.5f, 2.0f));
    EXPECT_EQ(mesh.TriangleCount(), 12u);
    ExpectXnaFrontFacing(mesh);
    const auto bounds = mesh.Bounds();
    EXPECT_FLOAT_EQ(bounds.Min.Y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Z, 2.0f);
}

TEST(MeshData, CylinderAndTorusAreFrontFacing)
{
    MeshData cyl;
    cyl.AddCylinder(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.3f, 1.0f, 12, true);
    ExpectXnaFrontFacing(cyl);
    MeshData torus;
    torus.AddTorus(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), 0.18f, 0.02f, 16, 8);
    ExpectXnaFrontFacing(torus);
}

TEST(MeshData, LoftSmoothNormalsPointOutwards)
{
    // A simple lofted tube of three square rings along +Y.
    std::vector<std::vector<Vector3>> rings;
    for (int r = 0; r < 3; ++r) {
        const float y = static_cast<float>(r);
        rings.push_back({Vector3(-1, y, -1), Vector3(-1, y, 1), Vector3(1, y, 1), Vector3(1, y, -1)});
    }
    MeshData loft;
    loft.AddLoft(rings, true);
    loft.ComputeSmoothNormals();
    ExpectXnaFrontFacing(loft);
    for (const auto& v : loft.vertices) {
        // Outward: the normal should point away from the tube's axis.
        EXPECT_GT(v.normal.X * v.position.X + v.normal.Z * v.position.Z, 0.0f);
    }
}

TEST(MeshData, FlipWindingReversesFacing)
{
    MeshData mesh;
    mesh.AddBox(Vector3(0, 0, 0), Vector3(1, 1, 1));
    mesh.FlipWinding();
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t) {
        const Vector3 emitted = mesh.EmittedTriangleNormal(t);
        const Vector3 authored = mesh.vertices[mesh.indices[t * 3]].normal;
        EXPECT_LT(Vector3::Dot(emitted, authored), 0.0f) << "normals must flip with the winding";
    }
}
