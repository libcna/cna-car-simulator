// Wheel geometry: revolved tyre with a real cross-section, alloy rim with a lip, dish, twin
// spokes and a centre cap, brake disc with caliper and hub carrier behind the spokes.
#include "CarBody.hpp"

namespace CarSim::Render::CarBody
{
    namespace
    {
        /// Tapered bar between two rectangles (centre, half sizes) in the wheel frame; flat shaded.
        void AddTaperedBar(MeshData& mesh, const Vector3& c0, const Vector3& right0, const Vector3& up0, const Vector3& c1, const Vector3& right1,
                           const Vector3& up1)
        {
            const Vector3 p[8] = {c0 - right0 - up0, c0 + right0 - up0, c0 + right0 + up0, c0 - right0 + up0,
                                  c1 - right1 - up1, c1 + right1 - up1, c1 + right1 + up1, c1 - right1 + up1};
            MeshData local;
            std::uint32_t id[8];
            for (int i = 0; i < 8; ++i) id[i] = local.AddVertex(p[i], Vector3(0, 1, 0), Vector2(0, 0), kWhite);
            // Sides (counter-clockwise from outside is verified afterwards by flipping against the centroid).
            local.AddQuad(id[0], id[1], id[5], id[4]);
            local.AddQuad(id[1], id[2], id[6], id[5]);
            local.AddQuad(id[2], id[3], id[7], id[6]);
            local.AddQuad(id[3], id[0], id[4], id[7]);
            local.AddQuad(id[3], id[2], id[1], id[0]);
            local.AddQuad(id[4], id[5], id[6], id[7]);
            Vector3 centre(0, 0, 0);
            for (const auto& q : p) centre = centre + q;
            centre = centre * (1.0f / 8.0f);
            for (std::size_t t = 0; t < local.TriangleCount(); ++t) {
                const Vector3 n = -local.EmittedTriangleNormal(t);
                const Vector3 a = local.vertices[local.indices[t * 3]].position;
                if (Vector3::Dot(n, a - centre) < 0.0f) std::swap(local.indices[t * 3 + 1], local.indices[t * 3 + 2]);
            }
            local.MakeFlatShaded();
            mesh.Append(local, Matrix::getIdentityProperty());
        }
    }

    void BuildWheel(const CarStyle& style, const float hw, MeshData& tyre, MeshData& rim, MeshData& disc)
    {
        const float r = style.wheelRadius;
        const float rr = std::min(style.rimRadius, r - 0.07f);
        const Vector3 axis(1, 0, 0);
        const Vector3 origin(0, 0, 0);

        // ---- Tyre: bead, sidewall bulge, shoulder, tread, back inside. Normal = cross(circumferential, profile).
        std::vector<Vector2> tyreProfile = {
            {rr + 0.002f, -hw * 0.80f}, {rr + 0.014f, -hw * 0.94f}, {r - 0.062f, -hw * 1.00f}, {r - 0.032f, -hw * 0.97f}, {r - 0.012f, -hw * 0.86f},
            {r, -hw * 0.72f}, {r, -hw * 0.36f}, {r, 0.0f}, {r, hw * 0.36f}, {r, hw * 0.72f},
            {r - 0.012f, hw * 0.86f}, {r - 0.032f, hw * 0.97f}, {r - 0.062f, hw * 1.00f}, {rr + 0.014f, hw * 0.94f}, {rr + 0.002f, hw * 0.80f},
            {rr - 0.008f, hw * 0.78f}, {rr - 0.008f, -hw * 0.78f}, {rr + 0.002f, -hw * 0.80f}};
        AddRevolve(tyre, tyreProfile, origin, axis, 44, 14.0f, true);

        // ---- Rim: outer lip, dish to the spoke plane, hub and centre cap (outer side, +x).
        std::vector<Vector2> dish = {{rr + 0.004f, hw * 0.78f}, {rr - 0.006f, hw * 0.90f}, {rr - 0.020f, hw * 0.84f}, {rr - 0.026f, hw * 0.58f},
                                     {0.080f, hw * 0.44f}, {0.080f, hw * 0.34f}, {0.046f, hw * 0.34f}, {0.046f, hw * 0.56f}, {0.030f, hw * 0.60f},
                                     {0.0f, hw * 0.60f}};
        AddRevolve(rim, dish, origin, axis, 36, 1.0f, true);
        // Inner barrel seen through the spokes, and the inner flange.
        std::vector<Vector2> barrel = {{rr - 0.026f, hw * 0.58f}, {rr - 0.030f, -hw * 0.70f}, {rr + 0.002f, -hw * 0.80f}};
        AddRevolve(rim, barrel, origin, axis, 36, 1.0f, true);
        // Twin spokes.
        const int spokes = std::max(4, style.rimSpokes);
        for (int s = 0; s < spokes; ++s) {
            const float a = static_cast<float>(s) / static_cast<float>(spokes) * 2.0f * kPi;
            const Matrix rot = Matrix::CreateRotationX(a);
            for (const float lateral : {-0.021f, 0.021f}) {
                const Vector3 c0 = Vector3::Transform(Vector3(hw * 0.40f, 0.060f, lateral), rot);
                const Vector3 c1 = Vector3::Transform(Vector3(hw * 0.56f, rr - 0.022f, lateral * 1.9f), rot);
                const Vector3 right0 = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 0.012f), rot);
                const Vector3 right1 = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 0.010f), rot);
                const Vector3 up0(0.013f, 0.0f, 0.0f);
                const Vector3 up1(0.007f, 0.0f, 0.0f);
                MeshData bar;
                AddTaperedBar(bar, c0, right0, up0, c1, right1, up1);
                rim.Append(bar, Matrix::getIdentityProperty());
            }
        }

        // ---- Brake disc, hub carrier and caliper (BrakeDisc material, dark metal).
        std::vector<Vector2> discProfile = {{0.135f, -0.011f}, {0.135f, 0.011f}, {0.072f, 0.011f}, {0.072f, -0.011f}, {0.135f, -0.011f}};
        AddRevolve(disc, discProfile, origin, axis, 32, 1.0f, false);
        disc.AddCylinder(Vector3(-0.07f, 0, 0), axis, 0.075f, 0.075f, 16, true);
        {
            MeshData caliper;
            caliper.AddBox(Vector3(-0.025f, 0.085f, -0.055f), Vector3(0.025f, 0.140f, 0.055f), 1.0f);
            caliper.Transform(Matrix::CreateRotationX(kPi * 0.72f));
            disc.Append(caliper, Matrix::getIdentityProperty());
        }
    }
}
