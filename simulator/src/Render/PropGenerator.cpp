#include "CarSim/Render/PropGenerator.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Render
{
    using Map::PropType;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        const Color kWhite(255, 255, 255, 255);

        void Quad(MeshData& m, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& n)
        {
            m.AddQuad(a, b, c, d, n, Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), kWhite);
        }

        /// Local frame: +z is the prop's facing direction, x to the right, y up.
        Matrix WorldOf(const Map::PlacedProp& p)
        {
            return Matrix::CreateRotationY(-p.headingRad + 3.14159265f) * Matrix::CreateTranslation(p.position);
        }
    }

    void PropGenerator::Generate(const Map::PlacedProp& p, PropMeshes& out)
    {
        MeshData metal, wood, concrete, white, black, orange, refWhite, glass, red;
        const float s = p.scale;
        switch (p.type) {
            case PropType::Delineator: {
                // Z 11: 12 x 12 cm white post, 1.05 m above ground, black band with the reflector facing the driver.
                white.AddBox(Vector3(-0.06f, -0.2f, -0.06f), Vector3(0.06f, 1.05f, 0.06f), 1.0f);
                black.AddBox(Vector3(-0.065f, 0.70f, -0.065f), Vector3(0.065f, 0.95f, 0.065f), 1.0f);
                // Reflector on the face towards the approaching driver (+z): orange (right side) or white.
                MeshData& ref = p.reflectorRight ? orange : refWhite;
                Quad(ref, Vector3(-0.03f, 0.76f, 0.068f), Vector3(0.03f, 0.76f, 0.068f), Vector3(0.03f, 0.90f, 0.068f), Vector3(-0.03f, 0.90f, 0.068f), Vector3(0, 0, 1));
                Quad(refWhite, Vector3(0.03f, 0.76f, -0.068f), Vector3(-0.03f, 0.76f, -0.068f), Vector3(-0.03f, 0.90f, -0.068f), Vector3(0.03f, 0.90f, -0.068f), Vector3(0, 0, -1));
                break;
            }
            case PropType::Lamp: {
                metal.AddCylinder(Vector3(0, -0.2f, 0), Vector3(0, 1, 0), 0.08f * s, 7.2f * s, 10, false);
                metal.AddCylinder(Vector3(0, 6.8f * s, 0), Vector3(0, 0.25f, 1.0f), 0.05f * s, 1.3f * s, 8, false);
                metal.AddBox(Vector3(-0.16f * s, 6.95f * s, 1.05f * s), Vector3(0.16f * s, 7.15f * s, 1.65f * s), 1.0f);
                white.AddBox(Vector3(-0.14f * s, 6.93f * s, 1.08f * s), Vector3(0.14f * s, 6.96f * s, 1.62f * s), 1.0f);
                concrete.AddCylinder(Vector3(0, -0.2f, 0), Vector3(0, 1, 0), 0.16f * s, 0.6f, 10, true);
                break;
            }
            case PropType::Bench: {
                for (const float x : {-0.75f, 0.75f}) {
                    metal.AddBox(Vector3(x - 0.03f, -0.05f, -0.22f), Vector3(x + 0.03f, 0.44f, 0.22f), 1.0f);
                    metal.AddBox(Vector3(x - 0.03f, 0.44f, -0.24f), Vector3(x + 0.03f, 0.92f, -0.16f), 1.0f);
                }
                for (int i = 0; i < 4; ++i) {
                    const float z = -0.2f + static_cast<float>(i) * 0.11f;
                    wood.AddBox(Vector3(-0.9f, 0.42f, z), Vector3(0.9f, 0.46f, z + 0.09f), 1.0f);
                }
                for (int i = 0; i < 3; ++i) {
                    const float y = 0.55f + static_cast<float>(i) * 0.12f;
                    wood.AddBox(Vector3(-0.9f, y, -0.24f), Vector3(0.9f, y + 0.09f, -0.20f), 1.0f);
                }
                break;
            }
            case PropType::BusStop: {
                // Shelter: three posts, roof, back and one side panel; the open side faces the road (+z).
                for (const float x : {-1.9f, 0.0f, 1.9f}) {
                    metal.AddBox(Vector3(x - 0.04f, -0.05f, -1.0f), Vector3(x + 0.04f, 2.5f, -0.92f), 1.0f);
                }
                for (const float x : {-1.9f, 1.9f}) {
                    metal.AddBox(Vector3(x - 0.04f, -0.05f, 0.9f), Vector3(x + 0.04f, 2.5f, 0.98f), 1.0f);
                }
                metal.AddBox(Vector3(-2.1f, 2.5f, -1.2f), Vector3(2.1f, 2.62f, 1.2f), 1.0f);
                Quad(glass, Vector3(-1.86f, 0.3f, -0.95f), Vector3(1.86f, 0.3f, -0.95f), Vector3(1.86f, 2.45f, -0.95f), Vector3(-1.86f, 2.45f, -0.95f), Vector3(0, 0, 1));
                Quad(glass, Vector3(1.86f, 0.3f, -0.97f), Vector3(-1.86f, 0.3f, -0.97f), Vector3(-1.86f, 2.45f, -0.97f), Vector3(1.86f, 2.45f, -0.97f), Vector3(0, 0, -1));
                Quad(glass, Vector3(-1.93f, 0.3f, 0.9f), Vector3(-1.93f, 0.3f, -0.9f), Vector3(-1.93f, 2.45f, -0.9f), Vector3(-1.93f, 2.45f, 0.9f), Vector3(-1, 0, 0));
                Quad(glass, Vector3(-1.91f, 0.3f, -0.9f), Vector3(-1.91f, 0.3f, 0.9f), Vector3(-1.91f, 2.45f, 0.9f), Vector3(-1.91f, 2.45f, -0.9f), Vector3(1, 0, 0));
                // Bench inside and a timetable board.
                wood.AddBox(Vector3(-1.5f, 0.42f, -0.85f), Vector3(1.5f, 0.48f, -0.45f), 1.0f);
                metal.AddBox(Vector3(-1.5f, 0.0f, -0.6f), Vector3(-1.44f, 0.42f, -0.55f), 1.0f);
                metal.AddBox(Vector3(1.44f, 0.0f, -0.6f), Vector3(1.5f, 0.42f, -0.55f), 1.0f);
                white.AddBox(Vector3(-1.85f, 1.3f, -0.9f), Vector3(-1.86f, 1.9f, -0.5f), 1.0f);
                // Stop sign post (IJ 4c) at the open corner.
                metal.AddCylinder(Vector3(2.4f, -0.1f, 0.6f), Vector3(0, 1, 0), 0.03f, 2.6f, 8, false);
                break;
            }
            case PropType::Fence: {
                const float length = std::max(2.0f, p.length);
                const int posts = static_cast<int>(length / 2.0f) + 1;
                for (int i = 0; i < posts; ++i) {
                    const float x = -length * 0.5f + static_cast<float>(i) * length / static_cast<float>(posts - 1);
                    wood.AddBox(Vector3(x - 0.05f, -0.1f, -0.05f), Vector3(x + 0.05f, 1.25f, 0.05f), 1.0f);
                }
                wood.AddBox(Vector3(-length * 0.5f, 0.45f, 0.05f), Vector3(length * 0.5f, 0.52f, 0.09f), 1.0f);
                wood.AddBox(Vector3(-length * 0.5f, 1.05f, 0.05f), Vector3(length * 0.5f, 1.12f, 0.09f), 1.0f);
                const int pickets = static_cast<int>(length / 0.15f);
                for (int i = 0; i < pickets; ++i) {
                    const float x = -length * 0.5f + 0.05f + static_cast<float>(i) * 0.15f;
                    wood.AddBox(Vector3(x, 0.1f, 0.09f), Vector3(x + 0.07f, 1.2f, 0.11f), 1.0f);
                }
                break;
            }
            case PropType::Wall: {
                const float length = std::max(2.0f, p.length);
                concrete.AddBox(Vector3(-length * 0.5f, -0.2f, -0.2f), Vector3(length * 0.5f, 1.7f, 0.2f), 0.5f);
                concrete.AddBox(Vector3(-length * 0.5f - 0.04f, 1.7f, -0.26f), Vector3(length * 0.5f + 0.04f, 1.82f, 0.26f), 0.5f);
                for (float x = -length * 0.5f; x < length * 0.5f; x += 3.0f) {
                    concrete.AddBox(Vector3(x - 0.08f, -0.2f, -0.28f), Vector3(x + 0.08f, 1.9f, 0.28f), 0.5f);
                }
                break;
            }
            case PropType::Gate: {
                // Forest barrier: two posts and a red-white beam across the track (open at 70 degrees).
                metal.AddCylinder(Vector3(-2.2f, -0.2f, 0), Vector3(0, 1, 0), 0.07f, 1.2f, 8, true);
                metal.AddCylinder(Vector3(2.2f, -0.2f, 0), Vector3(0, 1, 0), 0.07f, 1.2f, 8, true);
                MeshData beam;
                beam.AddBox(Vector3(0.0f, -0.05f, -0.05f), Vector3(4.2f, 0.05f, 0.05f), 1.0f);
                beam.Transform(Matrix::CreateRotationZ(1.22f) * Matrix::CreateTranslation(-2.2f, 1.0f, 0.0f));
                red.Append(beam, Matrix::getIdentityProperty());
                break;
            }
            case PropType::TimberStack: {
                const float length = std::max(3.0f, p.length);
                int perRow = 6;
                float y = 0.0f;
                for (int row = 0; row < 4; ++row, y += 0.34f, --perRow) {
                    for (int i = 0; i < perRow; ++i) {
                        const float z = (static_cast<float>(i) - static_cast<float>(perRow - 1) * 0.5f) * 0.4f;
                        wood.AddCylinder(Vector3(-length * 0.5f, y + 0.2f, z), Vector3(1, 0, 0), 0.19f, length, 9, true, kWhite, 0.5f);
                    }
                }
                break;
            }
            case PropType::Hydrant: {
                red.AddCylinder(Vector3(0, -0.1f, 0), Vector3(0, 1, 0), 0.09f, 0.85f, 10, true);
                red.AddCylinder(Vector3(-0.2f, 0.55f, 0), Vector3(1, 0, 0), 0.05f, 0.4f, 8, true);
                red.AddCylinder(Vector3(0, 0.75f, 0), Vector3(0, 1, 0), 0.11f, 0.12f, 10, true);
                break;
            }
            case PropType::Bin: {
                metal.AddCylinder(Vector3(0, -0.1f, 0), Vector3(0, 1, 0), 0.03f, 1.1f, 8, false);
                white.AddCylinder(Vector3(0, 0.55f, 0), Vector3(0, 1, 0), 0.2f, 0.5f, 12, true);
                black.AddCylinder(Vector3(0, 1.05f, 0), Vector3(0, 1, 0), 0.21f, 0.04f, 12, true);
                break;
            }
            case PropType::Unknown:
                break;
        }
        const Matrix world = WorldOf(p);
        out.metal.Append(metal, world);
        out.wood.Append(wood, world);
        out.concrete.Append(concrete, world);
        out.white.Append(white, world);
        out.black.Append(black, world);
        out.reflectorOrange.Append(orange, world);
        out.reflectorWhite.Append(refWhite, world);
        out.glass.Append(glass, world);
        out.red.Append(red, world);
    }
}
