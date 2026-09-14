// Cockpit geometry: dashboard with binnacle and centre stack, steering wheel and column, gear
// lever, handbrake, seats, door cards, inner shell (headliner, pillars, tailgate), interior
// mirror, sun visors. Built in the vehicle frame from the definition's cockpit placement.
#include "CarBody.hpp"
#include "CarSim/Sim/Units.hpp"

namespace CarSim::Render::CarBody
{
    namespace
    {
        void AddSeat(CarPart& fabric, CarPart& plastic, const float x, const float floorY, const float zCushion, const float width, const bool rear)
        {
            const float cushionTop = floorY + 0.30f;
            const float depth = rear ? 0.50f : 0.50f;
            // Cushion with side bolsters.
            AddRoundedBox(fabric.mesh, Vector3(width, 0.13f, depth), 0.04f, Matrix::CreateTranslation(x, cushionTop - 0.065f, zCushion));
            if (!rear) {
                for (const float side : {-1.0f, 1.0f}) {
                    AddRoundedBox(fabric.mesh, Vector3(0.10f, 0.15f, depth - 0.04f), 0.03f, Matrix::CreateTranslation(x + side * (width * 0.5f - 0.05f), cushionTop - 0.02f, zCushion + 0.02f));
                }
            }
            // Backrest tilted back, with bolsters and a head restraint on two posts.
            const float tilt = 0.34f;   // radians back from vertical
            const float backH = 0.62f;
            const Vector3 backBase(x, cushionTop - 0.02f, zCushion + depth * 0.5f - 0.05f);
            const Matrix backFrame = Matrix::CreateRotationX(-tilt) * Matrix::CreateTranslation(backBase);
            AddRoundedBox(fabric.mesh, Vector3(width - 0.02f, backH, 0.11f), 0.035f, Matrix::CreateTranslation(0.0f, backH * 0.5f, 0.0f) * backFrame);
            if (!rear) {
                for (const float side : {-1.0f, 1.0f}) {
                    AddRoundedBox(fabric.mesh, Vector3(0.09f, backH - 0.08f, 0.14f), 0.03f,
                                  Matrix::CreateTranslation(side * (width * 0.5f - 0.05f), backH * 0.5f - 0.02f, 0.02f) * backFrame);
                }
            }
            const float headY = backH + 0.10f;
            AddRoundedBox(fabric.mesh, Vector3(0.26f, 0.15f, 0.09f), 0.03f, Matrix::CreateTranslation(0.0f, headY, 0.0f) * backFrame);
            for (const float side : {-0.06f, 0.06f}) {
                MeshData post;
                post.AddCylinder(Vector3(side, backH - 0.02f, 0.0f), Vector3(0, 1, 0), 0.006f, 0.14f, 6, false);
                plastic.mesh.Append(post, backFrame);
            }
            // Seat base rails / plinth.
            AddBoxTo(plastic, Vector3(x, floorY + 0.09f, zCushion), Vector3(width - 0.12f, 0.16f, depth - 0.12f));
        }
    }

    void BuildCockpit(CarModel& model, const CarStyle& style, const Sim::VehicleDefinition* definition, const SkinGrid& skin,
                      const std::vector<std::vector<CarMaterial>>& skinMaterials, const float zCowl, const float zRoofFront, const float zSideGlassRear)
    {
        Sim::VisualDefinition vis;
        if (definition) vis = definition->visual;
        const float floorY = style.rideHeight + 0.05f;
        const float belt = style.beltHeight;
        const float cabinHalf = style.width * 0.5f - 0.11f;
        const float zR = style.RearZ();
        const float wsBase = skin.rings.empty() ? 0.88f : [&] {
            // Windshield base height: top of the skin at the cowl.
            float best = 1e9f, y = 0.88f;
            for (std::size_t r = 0; r < skin.stations.size(); ++r) {
                const float d = std::fabs(skin.stations[r] - (zCowl + 0.02f));
                if (d < best) { best = d; y = skin.rings[r][Ring::kTop].Y; }
            }
            return y;
        }();

        CarPart interior = MakePart("interior", CarMaterial::Interior, CarPart::Role::Interior);
        CarPart light = MakePart("interior_light", CarMaterial::InteriorLight, CarPart::Role::Interior);
        CarPart fabric = MakePart("interior_fabric", CarMaterial::Fabric, CarPart::Role::Interior);
        CarPart gloss = MakePart("interior_gloss", CarMaterial::GlossBlack, CarPart::Role::Interior);
        CarPart vents = MakePart("interior_vents", CarMaterial::Grille, CarPart::Role::Interior);
        CarPart chrome = MakePart("interior_chrome", CarMaterial::Chrome, CarPart::Role::Interior);
        CarPart cluster = MakePart("cluster", CarMaterial::Cluster, CarPart::Role::Interior);
        CarPart mirror = MakePart("mirror_face", CarMaterial::Chrome, CarPart::Role::Interior);

        // ---- Inner shell: headliner, pillars, door cards, tailgate from the skin ---------------
        {
            const int n = Ring::kPoints;
            for (std::size_t r = 0; r + 1 < skin.rings.size(); ++r) {
                const float zc = 0.5f * (skin.stations[r] + skin.stations[r + 1]);
                if (zc < zCowl - 0.03f || zc > zR - 0.03f) continue;
                for (int seg = 0; seg < n; ++seg) {
                    const CarMaterial m = skinMaterials[r][static_cast<std::size_t>(seg)];
                    if (m == CarMaterial::Glass) continue;
                    const int rs = Ring::RightSegment(seg);
                    const int s1 = (seg + 1) % n;
                    const auto& a0 = skin.rings[r];
                    const auto& a1 = skin.rings[r + 1];
                    const auto& n0 = skin.normals[r];
                    const auto& n1 = skin.normals[r + 1];
                    CarPart* target = nullptr;
                    const float inset = 0.0f;   // flush with the skin: no slit between trim and glass
                    if (rs >= Ring::kGlassBase) {
                        target = zc > zSideGlassRear + 0.3f ? &interior : &light;   // headliner and pillars; tailgate inner is dark
                    } else if (rs >= Ring::kRockerTop && zc > zCowl + 0.05f) {
                        target = &interior;   // door cards and rear quarter trim
                    }
                    if (!target) continue;
                    MeshData& dst = target->mesh;
                    const auto add = [&](const Vector3& p, const Vector3& nn, float uu, float vv) {
                        return dst.AddVertex(p - nn * inset, -nn, Vector2(uu * 8.0f, vv * 8.0f), kWhite);
                    };
                    const float uA = skin.u[static_cast<std::size_t>(seg)];
                    const float uB = s1 == 0 ? 1.0f : skin.u[static_cast<std::size_t>(s1)];
                    const float v0 = skin.V(skin.stations[r]);
                    const float v1 = skin.V(skin.stations[r + 1]);
                    const std::uint32_t i00 = add(a0[static_cast<std::size_t>(seg)], n0[static_cast<std::size_t>(seg)], uA, v0);
                    const std::uint32_t i01 = add(a0[static_cast<std::size_t>(s1)], n0[static_cast<std::size_t>(s1)], uB, v0);
                    const std::uint32_t i11 = add(a1[static_cast<std::size_t>(s1)], n1[static_cast<std::size_t>(s1)], uB, v1);
                    const std::uint32_t i10 = add(a1[static_cast<std::size_t>(seg)], n1[static_cast<std::size_t>(seg)], uA, v1);
                    dst.AddQuad(i00, i10, i11, i01);
                }
            }
        }

        // ---- Floor, tunnel, firewall -------------------------------------------------------
        AddBoxTo(interior, Vector3(0.0f, floorY - 0.01f, 0.5f * (zCowl + zR - 0.3f)), Vector3(2.0f * (cabinHalf + 0.06f), 0.03f, (zR - 0.3f) - zCowl));
        AddBoxTo(interior, Vector3(0.0f, floorY + 0.06f, 0.5f * (zCowl + 0.9f)), Vector3(0.30f, 0.14f, 0.9f - zCowl));
        AddBoxTo(interior, Vector3(0.0f, 0.5f * (floorY + wsBase - 0.40f), zCowl + 0.02f), Vector3(2.0f * (cabinHalf + 0.06f), wsBase - 0.40f - floorY, 0.04f));

        // ---- Dashboard: closed profile in (z, y) lofted across x with a driver-side binnacle ---
        {
            const float zFront = zCowl + 0.01f;
            const float yTopFront = wsBase - 0.006f;
            const float yTopRear = wsBase + 0.025f;
            const float zEdge = zCowl + 0.31f;
            const float yFace = yTopRear - 0.14f;
            const float zFace = zEdge + 0.02f;
            const float yKnee = floorY + 0.28f;
            std::vector<std::vector<Vector3>> rings;
            const int columns = 18;
            for (int c = 0; c <= columns; ++c) {
                const float t = static_cast<float>(c) / static_cast<float>(columns);
                const float x = -cabinHalf + 2.0f * cabinHalf * t;
                // Binnacle hump on the driver's side, centre stack pushed towards the occupants.
                const float hump = 0.075f * std::exp(-std::pow((x - vis.clusterCenter.X) / 0.20f, 2.0f) * 2.0f);
                const float stack = 0.07f * std::exp(-std::pow(x / 0.19f, 2.0f) * 2.0f);
                std::vector<Vector3> ring;
                const auto p = [&](float z, float y) { ring.emplace_back(x, y, z); };
                p(zFront, yTopFront);
                p(zFront + 0.10f, yTopFront + 0.008f + hump * 0.3f);
                p(zFront + 0.26f, yTopRear - 0.004f + hump);
                p(zEdge - 0.06f, yTopRear + hump * 0.8f);
                p(zEdge, yTopRear - 0.015f + hump * 0.4f);
                p(zEdge + 0.02f, yTopRear - 0.05f);           // rounded edge
                p(zFace + stack * 0.5f, yFace);               // upper face
                p(zFace + stack, yFace - 0.14f);              // lower face (centre stack)
                p(zFace + stack + 0.03f, yKnee + 0.06f);      // knee bolster
                p(zFace + stack * 0.6f, yKnee);               // bolster underside
                p(zFront + 0.05f, yKnee - 0.02f);             // underside back to the firewall
                p(zFront, yKnee + 0.10f);
                p(zFront, yTopFront - 0.10f);
                rings.push_back(std::move(ring));
            }
            MeshData slab;
            slab.AddLoft(rings, true);
            slab.ComputeSmoothNormals();
            bool up = false;
            for (std::size_t t = 0; t < slab.TriangleCount(); ++t) {
                const Vector3 nn = -slab.EmittedTriangleNormal(t);
                if (nn.Y > 0.5f) { up = true; break; }
            }
            if (!up) slab.FlipWinding();
            // End caps at the doors.
            for (const int end : {0, columns}) {
                const auto& ring = rings[static_cast<std::size_t>(end)];
                Vector3 centre(0, 0, 0);
                for (const auto& q : ring) centre = centre + q;
                centre = centre * (1.0f / static_cast<float>(ring.size()));
                const Vector3 nn(end == 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
                const std::uint32_t ci = slab.AddVertex(centre, nn, Vector2(0.5f, 0.5f), kWhite);
                std::vector<std::uint32_t> ids;
                for (const auto& q : ring) ids.push_back(slab.AddVertex(q, nn, Vector2(q.Z, q.Y), kWhite));
                for (std::size_t i = 0; i < ring.size(); ++i) {
                    const std::uint32_t a = ids[i], b = ids[(i + 1) % ring.size()];
                    if (end == 0) slab.AddTriangle(ci, b, a); else slab.AddTriangle(ci, a, b);
                }
            }
            interior.mesh.Append(slab, Matrix::getIdentityProperty());

            // Centre stack details: display, vents, knobs; outer vents at the dash ends.
            const float zStack = zFace + 0.07f + 0.004f;
            AddBoxTo(gloss, Vector3(0.0f, yFace + 0.005f, zStack - 0.002f), Vector3(0.22f, 0.07f, 0.008f));
            for (const float vx : {-0.085f, 0.085f}) {
                vents.mesh.AddQuad(Vector3(vx - 0.045f, yFace + 0.05f, zStack + 0.004f), Vector3(vx + 0.045f, yFace + 0.05f, zStack + 0.004f),
                                   Vector3(vx + 0.045f, yFace + 0.10f, zStack + 0.004f), Vector3(vx - 0.045f, yFace + 0.10f, zStack + 0.004f),
                                   Vector3(0, 0.3f, 1), Vector2(0, 1), Vector2(2, 1), Vector2(2, 0), Vector2(0, 0));
            }
            for (const float vx : {-cabinHalf + 0.12f, cabinHalf - 0.12f}) {
                const float zv = zFace + 0.004f;
                vents.mesh.AddQuad(Vector3(vx - 0.06f, yFace + 0.02f, zv), Vector3(vx + 0.06f, yFace + 0.02f, zv),
                                   Vector3(vx + 0.06f, yFace + 0.09f, zv), Vector3(vx - 0.06f, yFace + 0.09f, zv),
                                   Vector3(0, 0.3f, 1), Vector2(0, 1), Vector2(2, 1), Vector2(2, 0), Vector2(0, 0));
            }
            for (int k = 0; k < 3; ++k) {
                const float kx = -0.07f + 0.07f * static_cast<float>(k);
                gloss.mesh.AddCylinder(Vector3(kx, yFace - 0.05f, zStack - 0.14f * 0.5f + 0.06f), Vector3(0, 0, 1), 0.016f, 0.02f, 12, true);
            }
            AddBoxTo(gloss, Vector3(0.0f, yFace - 0.10f, zStack + 0.055f), Vector3(0.20f, 0.03f, 0.008f));   // hazard/buttons strip

            // Instrument binnacle: visor shell over the cluster face.
            {
                const Vector3 c = vis.clusterCenter;
                const float tilt = 0.32f;
                const Vector3 n(0.0f, std::sin(tilt), std::cos(tilt));
                const Vector3 upv(0.0f, std::cos(tilt), -std::sin(tilt));
                const Vector3 right(1.0f, 0.0f, 0.0f);
                const float w = 0.34f, h = w * 448.0f / 1024.0f;
                const Vector3 o = c + n * 0.003f;
                cluster.mesh.AddQuad(o - right * (w * 0.5f) - upv * (h * 0.5f), o + right * (w * 0.5f) - upv * (h * 0.5f),
                                     o + right * (w * 0.5f) + upv * (h * 0.5f), o - right * (w * 0.5f) + upv * (h * 0.5f), n,
                                     Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
                // Bezel behind the face and the visor arc above it.
                AddOrientedBox(interior, c - n * 0.03f, Vector3(w + 0.06f, h + 0.06f, 0.06f), 0.0f, -tilt);
                std::vector<std::vector<Vector3>> visor;
                for (const float x : {c.X - w * 0.5f - 0.04f, c.X + w * 0.5f + 0.04f}) {
                    std::vector<Vector3> ring;
                    const Vector3 centre(x, c.Y + 0.02f, c.Z + 0.02f);
                    const float R = h * 0.5f + 0.05f;
                    for (int k = 0; k <= 9; ++k) {
                        const float a = kPi * 0.15f + (kPi * 0.85f) * static_cast<float>(k) / 9.0f;   // from the driver side over the top to the front
                        ring.push_back(centre + upv * (std::sin(a) * R) + n * (std::cos(a) * R));
                    }
                    for (int k = 9; k >= 0; --k) {
                        const float a = kPi * 0.15f + (kPi * 0.85f) * static_cast<float>(k) / 9.0f;
                        ring.push_back(centre + upv * (std::sin(a) * (R - 0.012f)) + n * (std::cos(a) * (R - 0.012f)));
                    }
                    visor.push_back(std::move(ring));
                }
                MeshData shell;
                shell.AddLoft(visor, true);
                shell.MakeFlatShaded();
                interior.mesh.Append(shell, Matrix::getIdentityProperty());
            }
        }

        // ---- Steering wheel, column, stalks ------------------------------------------------
        CarPart steering = MakePart("steering_wheel", CarMaterial::Interior, CarPart::Role::SteeringWheel);
        {
            const float tilt = Sim::Units::DegToRad(vis.steeringWheelTiltDeg);
            const Vector3 n(0.0f, std::sin(tilt), std::cos(tilt));
            steering.pivot = vis.steeringWheelCenter;
            steering.axis = n;
            const float R = vis.steeringWheelDiameterM * 0.5f;
            MeshData wheel;
            wheel.AddTorus(Vector3(0, 0, 0), Vector3(0, 0, 1), R, 0.019f, 44, 12);
            AddRoundedBox(wheel, Vector3(0.15f, 0.048f, 0.10f), 0.03f, Matrix::CreateRotationX(kPi * 0.5f) * Matrix::CreateTranslation(0.0f, -0.005f, 0.0f));
            for (const float a : {0.0f, kPi, kPi * 1.5f}) {
                MeshData spoke;
                spoke.AddBox(Vector3(0.05f, -0.016f, -0.010f), Vector3(R - 0.008f, 0.016f, 0.012f), 1.0f);
                spoke.Transform(Matrix::CreateRotationZ(a));
                wheel.Append(spoke, Matrix::getIdentityProperty());
            }
            wheel.Transform(Matrix::CreateRotationX(-tilt));
            wheel.ComputeSmoothNormals();
            steering.mesh = wheel;
            // Column shroud (tapered) and two stalks.
            std::vector<Vector2> shroud = {{0.030f, -0.03f}, {0.042f, -0.10f}, {0.055f, -0.24f}, {0.0f, -0.24f}};
            MeshData column;
            AddRevolve(column, shroud, Vector3(0, 0, 0), Vector3(0, 0, 1), 16, 1.0f, true);
            column.Transform(Matrix::CreateRotationX(-tilt) * Matrix::CreateTranslation(vis.steeringWheelCenter));
            interior.mesh.Append(column, Matrix::getIdentityProperty());
            for (const float side : {-1.0f, 1.0f}) {
                MeshData stalk;
                stalk.AddCylinder(Vector3(side * 0.04f, -0.01f, -0.12f), Vector3(side, -0.15f, 0.25f), 0.009f, 0.12f, 8, true);
                stalk.Transform(Matrix::CreateRotationX(-tilt) * Matrix::CreateTranslation(vis.steeringWheelCenter));
                interior.mesh.Append(stalk, Matrix::getIdentityProperty());
            }
        }

        // ---- Centre console, gear lever, handbrake ----------------------------------------
        CarPart gearLever = MakePart("gear_lever", CarMaterial::Interior, CarPart::Role::GearLever);
        {
            const float consoleTop = floorY + 0.31f;
            AddBoxTo(interior, Vector3(0.0f, 0.5f * (floorY + consoleTop), 0.12f), Vector3(0.30f, consoleTop - floorY, 0.95f));
            AddBoxTo(gloss, Vector3(0.0f, consoleTop + 0.004f, -0.14f), Vector3(0.20f, 0.008f, 0.22f));   // gear surround plate
            const Vector3 base(0.0f, consoleTop + 0.006f, -0.14f);
            gearLever.pivot = base;
            gearLever.axis = Vector3(1, 0, 0);
            std::vector<Vector2> gaiter = {{0.062f, 0.0f}, {0.040f, 0.03f}, {0.022f, 0.06f}, {0.016f, 0.075f}, {0.0f, 0.075f}};
            AddRevolve(gearLever.mesh, gaiter, Vector3(0, 0, 0), Vector3(0, 1, 0), 14, 1.0f, true);
            gearLever.mesh.AddCylinder(Vector3(0, 0.07f, 0), Vector3(0, 1, 0), 0.011f, 0.10f, 10, false);
            AddEllipsoid(gearLever.mesh, Vector3(0, 0.185f, 0.0f), Vector3(0.024f, 0.03f, 0.034f), 6, 12);
            // Handbrake lever.
            MeshData brake;
            brake.AddBox(Vector3(-0.018f, -0.012f, -0.02f), Vector3(0.018f, 0.012f, 0.22f), 1.0f);
            brake.AddCylinder(Vector3(0.0f, 0.0f, 0.18f), Vector3(0, 0, 1), 0.015f, 0.06f, 10, true);
            brake.Transform(Matrix::CreateRotationX(0.28f) * Matrix::CreateTranslation(0.0f, consoleTop + 0.03f, 0.10f));
            interior.mesh.Append(brake, Matrix::getIdentityProperty());
            AddBoxTo(gloss, Vector3(0.0f, consoleTop + 0.015f, 0.12f), Vector3(0.06f, 0.03f, 0.08f));
            AddBoxTo(interior, Vector3(0.0f, consoleTop + 0.02f, 0.42f), Vector3(0.26f, 0.05f, 0.30f));   // rear of the console / armrest
        }

        // ---- Seats -------------------------------------------------------------------------
        AddSeat(fabric, interior, -0.37f, floorY, 0.30f, 0.50f, false);
        AddSeat(fabric, interior, 0.37f, floorY, 0.30f, 0.50f, false);
        {
            const float benchZ = 1.05f;
            AddRoundedBox(fabric.mesh, Vector3(2.0f * cabinHalf - 0.16f, 0.14f, 0.50f), 0.04f, Matrix::CreateTranslation(0.0f, floorY + 0.24f, benchZ));
            const Matrix backFrame = Matrix::CreateRotationX(-0.30f) * Matrix::CreateTranslation(0.0f, floorY + 0.28f, benchZ + 0.22f);
            AddRoundedBox(fabric.mesh, Vector3(2.0f * cabinHalf - 0.18f, 0.60f, 0.10f), 0.035f, Matrix::CreateTranslation(0.0f, 0.30f, 0.0f) * backFrame);
            for (const float x : {-0.36f, 0.36f}) {
                AddRoundedBox(fabric.mesh, Vector3(0.24f, 0.14f, 0.08f), 0.03f, Matrix::CreateTranslation(x, 0.69f, 0.0f) * backFrame);
            }
            // Parcel shelf behind the rear seat (hatchback, estate and SUV).
            if (style.body != CarStyle::Body::Sedan) {
                const float shelfY = belt - 0.02f;
                const float z0 = benchZ + 0.40f;
                const float z1 = zR - 0.32f;
                if (z1 > z0 + 0.1f) {
                    AddBoxTo(interior, Vector3(0.0f, shelfY, 0.5f * (z0 + z1)), Vector3(2.0f * cabinHalf - 0.06f, 0.025f, z1 - z0));
                }
            }
        }

        // ---- Door armrests, pulls, window switches ---------------------------------------------
        for (const float side : {-1.0f, 1.0f}) {
            const float zArm = -0.05f;
            const float xDoor = cabinHalf + 0.03f;
            AddRoundedBox(interior.mesh, Vector3(0.09f, 0.05f, 0.34f), 0.02f, Matrix::CreateTranslation(side * (xDoor - 0.04f), belt - 0.24f, zArm));
            AddBoxTo(gloss, Vector3(side * (xDoor - 0.06f), belt - 0.21f, zArm - 0.06f), Vector3(0.05f, 0.006f, 0.10f));
            AddRoundedBox(chrome.mesh, Vector3(0.02f, 0.03f, 0.11f), 0.008f, Matrix::CreateTranslation(side * (xDoor - 0.05f), belt - 0.12f, zArm - 0.30f));
        }

        // ---- Interior mirror and sun visors -------------------------------------------------
        {
            const Vector3 c = vis.mirrorCenter;
            AddRoundedBox(interior.mesh, Vector3(0.25f, 0.075f, 0.028f), 0.012f, Matrix::CreateTranslation(c + Vector3(0.0f, 0.0f, -0.016f)));
            AddBoxTo(interior, c + Vector3(0.0f, 0.06f, -0.05f), Vector3(0.022f, 0.09f, 0.022f));
            const Vector3 right(0.115f, 0, 0);
            const Vector3 upv(0, 0.032f, 0);
            const Vector3 n(0, 0, 1);
            const Vector3 o = c + Vector3(0, 0, 0.0005f);
            mirror.mesh.AddQuad(o - right - upv, o + right - upv, o + right + upv, o - right + upv, n,
                                Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
            const float zVisor = zRoofFront + 0.06f;
            float yHeader = style.height - 0.09f;
            for (std::size_t r = 0; r < skin.stations.size(); ++r) {
                if (std::fabs(skin.stations[r] - zVisor) < 0.03f) yHeader = skin.rings[r][Ring::kTop].Y - 0.045f;
            }
            for (const float x : {-0.42f, 0.42f}) {
                AddOrientedBox(light, Vector3(x, yHeader, zVisor - 0.02f), Vector3(0.30f, 0.012f, 0.13f), 0.0f, -0.35f);
            }
        }

        interior.cabin = light.cabin = fabric.cabin = steering.cabin = true;
        for (CarPart* p : {&interior, &light, &fabric, &gloss, &vents, &chrome, &cluster, &steering, &gearLever, &mirror}) {
            if (p->mesh.TriangleCount() > 0) model.parts.push_back(std::move(*p));
        }
    }
}
