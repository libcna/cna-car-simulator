// Cockpit geometry: dashboard with binnacle and centre stack, steering wheel and column, gear
// lever, handbrake, seats, door cards, inner shell (headliner, pillars, tailgate), interior
// mirror, sun visors. Built in the vehicle frame from the definition's cockpit placement.
// Also the cheap cabin block used by cars without a cockpit (traffic).
#include "CarBody.hpp"
#include "CarSim/Sim/Units.hpp"

namespace CarSim::Render::CarBody
{
    namespace
    {
        void AddSeat(CarPart& fabric, CarPart& plastic, const float x, const float floorY, const float zCushion, const float width, const bool rear)
        {
            const float cushionTop = floorY + 0.30f;
            const float depth = 0.50f;
            // Cushion with side bolsters.
            AddRoundedBox(fabric.mesh, Vector3(width, 0.13f, depth), 0.04f, Matrix::CreateTranslation(x, cushionTop - 0.065f, zCushion));
            if (!rear) {
                for (const float side : {-1.0f, 1.0f}) {
                    AddRoundedBox(fabric.mesh, Vector3(0.10f, 0.15f, depth - 0.04f), 0.03f, Matrix::CreateTranslation(x + side * (width * 0.5f - 0.05f), cushionTop - 0.02f, zCushion + 0.02f));
                }
            }
            // Backrest tilted back (+z is rearward, so a positive rotation about x leans the
            // frame's up axis towards the rear), with bolsters and a head restraint on two posts.
            const float tilt = 0.34f;   // radians back from vertical
            const float backH = 0.62f;
            const Vector3 backBase(x, cushionTop - 0.02f, zCushion + depth * 0.5f - 0.05f);
            const Matrix backFrame = Matrix::CreateRotationX(tilt) * Matrix::CreateTranslation(backBase);
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

        /// Copies the non-glass skin quads of the cabin as inward-facing trim.
        /// `pick(rightSegment, zCentre, yCentre)` returns the target part (or null to skip).
        template <typename Pick>
        void CopyInnerShell(const SkinGrid& skin, const std::vector<std::vector<CarMaterial>>& skinMaterials, const float zFrom, const float zTo,
                            const Pick& pick)
        {
            const int n = Ring::kPoints;
            for (std::size_t r = 0; r + 1 < skin.rings.size(); ++r) {
                const float zc = 0.5f * (skin.stations[r] + skin.stations[r + 1]);
                if (zc < zFrom || zc > zTo) continue;
                for (int seg = 0; seg < n; ++seg) {
                    if (skinMaterials[r][static_cast<std::size_t>(seg)] == CarMaterial::Glass) continue;
                    const int s1 = (seg + 1) % n;
                    const auto& a0 = skin.rings[r];
                    const auto& a1 = skin.rings[r + 1];
                    const float yc = 0.25f * (a0[static_cast<std::size_t>(seg)].Y + a0[static_cast<std::size_t>(s1)].Y +
                                              a1[static_cast<std::size_t>(seg)].Y + a1[static_cast<std::size_t>(s1)].Y);
                    CarPart* target = pick(Ring::RightSegment(seg), zc, yc);
                    if (!target) continue;
                    const auto& n0 = skin.normals[r];
                    const auto& n1 = skin.normals[r + 1];
                    MeshData& dst = target->mesh;
                    const auto add = [&](const Vector3& p, const Vector3& nn, float uu, float vv) {
                        return dst.AddVertex(p, -nn, Vector2(uu * 8.0f, vv * 8.0f), kWhite);
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

        /// A narrow cabin-facing lip follows the actual roof-rail segment on each side of
        /// the windscreen. The broad opaque quads of the outer skin cover too much of the
        /// driver's view if copied wholesale as interior pillar trim.
        void AddInnerWindshieldPillars(MeshData& mesh, const SkinGrid& skin, const float zCowl, const float zRoofFront)
        {
            for (std::size_t r = 0; r + 1 < skin.rings.size(); ++r) {
                const float zc = 0.5f * (skin.stations[r] + skin.stations[r + 1]);
                if (zc < zCowl || zc > zRoofFront) continue;
                for (const int seg : {Ring::kRail - 1, Ring::kPoints - Ring::kRail}) {
                    const int next = seg + 1;
                    const int rail = seg == Ring::kRail - 1 ? next : seg;
                    const auto add = [&](const std::size_t station, const int point) {
                        // The whole loft segment is wider than the visible pillar. Keep only
                        // its inner 40 % against the roof-rail edge on both mirrored sides.
                        const float blend = point == rail ? 0.0f : 0.60f;
                        const Vector3 p = Vector3::Lerp(skin.rings[station][static_cast<std::size_t>(point)],
                                                        skin.rings[station][static_cast<std::size_t>(rail)], blend);
                        Vector3 normal = Vector3::Lerp(skin.normals[station][static_cast<std::size_t>(point)],
                                                       skin.normals[station][static_cast<std::size_t>(rail)], blend);
                        normal.Normalize();
                        return mesh.AddVertex(p, -normal, Vector2(skin.u[static_cast<std::size_t>(point)] * 8.0f,
                                                                    skin.V(skin.stations[station]) * 8.0f), kWhite);
                    };
                    const std::uint32_t a = add(r, seg);
                    const std::uint32_t b = add(r, next);
                    const std::uint32_t c = add(r + 1, next);
                    const std::uint32_t d = add(r + 1, seg);
                    mesh.AddQuad(a, d, c, b);
                }
            }
        }

        /// Dashboard slab lofted across x from a closed (z, y) profile; returns the mesh with
        /// smooth normals and outward orientation.
        MeshData DashboardSlab(const std::vector<std::vector<Vector3>>& rings, const int columns)
        {
            MeshData slab;
            slab.AddLoft(rings, true);
            // The profile may run either way round; orient by the enclosed volume so the pad
            // faces up and the occupant-facing panels face the cabin.
            slab.OrientOutward();
            slab.ComputeSmoothNormals();
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
            return slab;
        }

        /// Splits a mesh's triangles into two parts by a centroid predicate.
        template <typename Pred>
        void SplitTriangles(const MeshData& src, MeshData& yes, MeshData& no, const Pred& pred)
        {
            for (std::size_t t = 0; t < src.TriangleCount(); ++t) {
                Vector3 c(0, 0, 0);
                for (int k = 0; k < 3; ++k) c = c + src.vertices[src.indices[t * 3 + static_cast<std::size_t>(k)]].position;
                c = c * (1.0f / 3.0f);
                MeshData& dst = pred(c) ? yes : no;
                std::uint32_t ids[3];
                for (int k = 0; k < 3; ++k) ids[k] = dst.AddVertex(src.vertices[src.indices[t * 3 + static_cast<std::size_t>(k)]]);
                dst.indices.push_back(ids[0]);
                dst.indices.push_back(ids[1]);
                dst.indices.push_back(ids[2]);
            }
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
            float best = 1e9f, y = 0.88f;
            for (std::size_t r = 0; r < skin.stations.size(); ++r) {
                const float d = std::fabs(skin.stations[r] - (zCowl + 0.02f));
                if (d < best) { best = d; y = skin.rings[r][Ring::kTop].Y; }
            }
            return y;
        }();

        CarPart interior = MakePart("interior", CarMaterial::Interior, CarPart::Role::Interior);
        CarPart mid = MakePart("interior_mid", CarMaterial::InteriorMid, CarPart::Role::Interior);
        CarPart accent = MakePart("interior_accent", CarMaterial::InteriorAccent, CarPart::Role::Interior);
        CarPart light = MakePart("interior_light", CarMaterial::InteriorLight, CarPart::Role::Interior);
        CarPart fabric = MakePart("interior_fabric", CarMaterial::Fabric, CarPart::Role::Interior);
        CarPart dashSoft = MakePart("dashboard_soft", CarMaterial::DashSoft, CarPart::Role::Interior);
        CarPart gloss = MakePart("interior_gloss", CarMaterial::GlossBlack, CarPart::Role::Interior);
        CarPart vents = MakePart("interior_vents", CarMaterial::Vent, CarPart::Role::Interior);
        CarPart chrome = MakePart("interior_chrome", CarMaterial::Chrome, CarPart::Role::Interior);
        CarPart cluster = MakePart("cluster", CarMaterial::Cluster, CarPart::Role::Interior);
        CarPart mirror = MakePart("mirror_face", CarMaterial::Chrome, CarPart::Role::Interior);

        // ---- Inner shell: pale headliner, narrow charcoal windscreen pillars and door cards,
        // lower door panels (mid), tailgate inner (dark).
        CopyInnerShell(skin, skinMaterials, zCowl - 0.03f, zR - 0.03f, [&](int rs, float zc, float yc) -> CarPart* {
            if (rs >= Ring::kGlassBase) {
                if (zc > zSideGlassRear + 0.3f) return &interior;
                // Segment 12 is the dark window seal. The narrow pillar lip below is built
                // separately so the skin's broad opaque cowl quads do not fill the side view.
                if (rs <= Ring::kGlassBase) return &interior;
                if (zc < zRoofFront) {
                    if (rs < Ring::kRail) return nullptr;
                    if (rs == Ring::kRail) return &mid;
                }
                return &light;
            }
            if (rs >= Ring::kRockerTop && zc > zCowl + 0.05f) {
                return yc < belt - 0.30f ? &mid : &interior;
            }
            return nullptr;
        });
        AddInnerWindshieldPillars(mid.mesh, skin, zCowl, zRoofFront);

        // ---- Floor, tunnel, firewall -------------------------------------------------------
        AddBoxTo(interior, Vector3(0.0f, floorY - 0.01f, 0.5f * (zCowl + zR - 0.3f)), Vector3(2.0f * (cabinHalf + 0.06f), 0.03f, (zR - 0.3f) - zCowl));
        AddBoxTo(interior, Vector3(0.0f, floorY + 0.06f, 0.5f * (zCowl + 0.9f)), Vector3(0.30f, 0.14f, 0.9f - zCowl));
        AddBoxTo(interior, Vector3(0.0f, 0.5f * (floorY + wsBase - 0.40f), zCowl + 0.02f), Vector3(2.0f * (cabinHalf + 0.06f), wsBase - 0.40f - floorY, 0.04f));

        // ---- Dashboard: closed profile in (z, y) lofted across x with a driver-side binnacle ---
        const float zFront = zCowl + 0.01f;
        const float yTopFront = wsBase - 0.006f;
        const float yTopRear = wsBase + 0.03f;
        const float zEdge = zCowl + 0.36f;
        const float yFace = yTopRear - 0.15f;
        const float zFace = zEdge + 0.01f;
        const float yKnee = floorY + 0.28f;
        {
            std::vector<std::vector<Vector3>> rings;
            const int columns = 20;
            for (int c = 0; c <= columns; ++c) {
                const float t = static_cast<float>(c) / static_cast<float>(columns);
                const float x = -cabinHalf + 2.0f * cabinHalf * t;
                // Binnacle hump on the driver's side, centre stack pushed towards the occupants.
                const float hump = 0.07f * std::exp(-std::pow((x - vis.clusterCenter.X) / 0.21f, 2.0f) * 2.0f);
                const float stack = 0.075f * std::exp(-std::pow(x / 0.19f, 2.0f) * 2.0f);
                std::vector<Vector3> ring;
                const auto p = [&](float z, float y) { ring.emplace_back(x, y, z); };
                p(zFront, yTopFront);
                p(zFront + 0.08f, yTopFront + 0.010f + hump * 0.25f);
                p(zFront + 0.20f, yTopRear - 0.006f + hump * 0.9f);
                p(zEdge - 0.08f, yTopRear + hump);
                p(zEdge - 0.03f, yTopRear - 0.004f + hump * 0.5f);
                p(zEdge + 0.005f, yTopRear - 0.022f);          // rounded edge
                p(zEdge + 0.018f, yTopRear - 0.05f);
                p(zFace + stack * 0.4f, yFace);                // upper face
                p(zFace + stack, yFace - 0.14f);               // lower face (centre stack)
                p(zFace + stack + 0.03f, yKnee + 0.07f);       // knee bolster
                p(zFace + stack * 0.6f, yKnee);                // bolster underside
                p(zFront + 0.05f, yKnee - 0.02f);              // underside back to the firewall
                p(zFront, yKnee + 0.10f);
                p(zFront, yTopFront - 0.10f);
                rings.push_back(std::move(ring));
            }
            const MeshData slab = DashboardSlab(rings, columns);
            // Two-tone: the top pad and upper face are dark, the lower face and knee area mid grey.
            SplitTriangles(slab, interior.mesh, mid.mesh, [&](const Vector3& c) { return c.Y > yFace - 0.02f || c.Z < zFront + 0.03f; });

            // A shallow padded passenger-side airbag panel follows the slope of the dash.
            // Its cloth-grain top and short lower bevel break up the broad unlit slab in
            // the windscreen without covering the instrument hood or the road view.
            const float panelLeft = 0.18f;
            const float panelRight = cabinHalf - 0.07f;
            const float panelFrontZ = zFront + 0.13f;
            const float panelRearZ = zEdge - 0.045f;
            const float panelFrontY = yTopFront + 0.020f;
            const float panelRearY = yTopRear + 0.010f;
            dashSoft.mesh.AddQuad(Vector3(panelLeft, panelFrontY, panelFrontZ), Vector3(panelLeft, panelRearY, panelRearZ),
                                Vector3(panelRight, panelRearY, panelRearZ), Vector3(panelRight, panelFrontY, panelFrontZ),
                                Vector3(0, 1, -0.12f), Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0));
            mid.mesh.AddQuad(Vector3(panelLeft, panelRearY - 0.016f, panelRearZ + 0.007f),
                             Vector3(panelRight, panelRearY - 0.016f, panelRearZ + 0.007f),
                             Vector3(panelRight, panelRearY, panelRearZ), Vector3(panelLeft, panelRearY, panelRearZ),
                             Vector3(0, 0, 1), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
            // The front of that compartment is a recessed soft-touch lid above the
            // glovebox. Leave the outer vent at the dash end and the centre stack clear.
            const float lidLeft = 0.18f;
            const float lidRight = cabinHalf - 0.22f;
            const float lidBottom = yFace + 0.012f;
            const float lidTop = yFace + 0.103f;
            const float lidZ = zFace + 0.025f;
            AddRoundedBox(accent.mesh, Vector3(lidRight - lidLeft, lidTop - lidBottom, 0.010f), 0.009f,
                          Matrix::CreateTranslation(0.5f * (lidLeft + lidRight), 0.5f * (lidBottom + lidTop), lidZ));
            dashSoft.mesh.AddQuad(Vector3(lidLeft + 0.008f, lidBottom + 0.008f, lidZ + 0.006f),
                                Vector3(lidRight - 0.008f, lidBottom + 0.008f, lidZ + 0.006f),
                                Vector3(lidRight - 0.008f, lidTop - 0.008f, lidZ + 0.006f),
                                Vector3(lidLeft + 0.008f, lidTop - 0.008f, lidZ + 0.006f),
                                Vector3(0, 0, 1), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));

            // Defroster outlets sit in the shallow cowl strip ahead of the soft pad.
            // Give each one a moulded rim, dark recess and directional slats so the
            // windscreen base reads as cabin hardware instead of a featureless slab.
            for (const float side : {-1.0f, 1.0f}) {
                const float width = side < 0.0f ? 0.22f : 0.30f;
                const float x = side < 0.0f ? -cabinHalf + 0.27f : cabinHalf - 0.28f;
                const float z = zFront + 0.095f;
                const float y = yTopFront + 0.020f;
                AddRoundedBox(mid.mesh, Vector3(width + 0.012f, 0.008f, 0.077f), 0.004f,
                              Matrix::CreateTranslation(x, y, z));
                AddBoxTo(gloss, Vector3(x, y + 0.005f, z), Vector3(width, 0.003f, 0.062f));
                vents.mesh.AddQuad(Vector3(x - width * 0.5f + 0.007f, y + 0.007f, z - 0.024f),
                                   Vector3(x - width * 0.5f + 0.007f, y + 0.007f, z + 0.024f),
                                   Vector3(x + width * 0.5f - 0.007f, y + 0.007f, z + 0.024f),
                                   Vector3(x + width * 0.5f - 0.007f, y + 0.007f, z - 0.024f),
                                   Vector3(0, 1, 0), Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0));
            }

            // Centre stack details: display, vents, knobs; outer vents at the dash ends; glovebox line.
            const float zStack = zFace + 0.075f + 0.004f;
            // The radio sits in a shallow moulded surround rather than a dark rectangle
            // painted directly onto the dashboard face. All trim joins existing material
            // batches; only the surfaces seen from the driving position are modelled.
            AddRoundedBox(accent.mesh, Vector3(0.275f, 0.091f, 0.012f), 0.012f,
                          Matrix::CreateTranslation(0.0f, yFace + 0.005f, zStack - 0.012f));
            AddBoxTo(gloss, Vector3(0.0f, yFace + 0.005f, zStack - 0.002f), Vector3(0.22f, 0.07f, 0.008f));
            for (const float side : {-1.0f, 1.0f}) {
                chrome.mesh.AddCylinder(Vector3(side * 0.124f, yFace + 0.004f, zStack + 0.003f),
                                        Vector3(0, 0, 1), 0.011f, 0.006f, 12, true);
                AddRoundedBox(gloss.mesh, Vector3(0.019f, 0.013f, 0.004f), 0.003f,
                              Matrix::CreateTranslation(side * 0.124f, yFace - 0.024f, zStack + 0.006f));
            }
            for (const float x : {-0.067f, -0.022f, 0.022f, 0.067f}) {
                AddRoundedBox(chrome.mesh, Vector3(0.024f, 0.004f, 0.003f), 0.001f,
                              Matrix::CreateTranslation(x, yFace - 0.051f, zStack + 0.035f));
            }
            for (const float vx : {-0.085f, 0.085f}) {
                vents.mesh.AddQuad(Vector3(vx - 0.045f, yFace + 0.05f, zStack + 0.004f), Vector3(vx + 0.045f, yFace + 0.05f, zStack + 0.004f),
                                   Vector3(vx + 0.045f, yFace + 0.10f, zStack + 0.004f), Vector3(vx - 0.045f, yFace + 0.10f, zStack + 0.004f),
                                   Vector3(0, 0.3f, 1), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
            }
            for (const float vx : {-cabinHalf + 0.13f, cabinHalf - 0.13f}) {
                const float zv = zFace + 0.006f;
                AddRoundedBox(accent.mesh, Vector3(0.17f, 0.11f, 0.012f), 0.009f,
                              Matrix::CreateTranslation(vx, yFace + 0.055f, zv - 0.006f));
                vents.mesh.AddQuad(Vector3(vx - 0.065f, yFace + 0.02f, zv), Vector3(vx + 0.065f, yFace + 0.02f, zv),
                                   Vector3(vx + 0.065f, yFace + 0.09f, zv), Vector3(vx - 0.065f, yFace + 0.09f, zv),
                                   Vector3(0, 0.3f, 1), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
                AddBoxTo(gloss, Vector3(vx, yFace + 0.055f, zv - 0.004f), Vector3(0.15f, 0.09f, 0.006f));
            }
            // A narrow passenger-side satin seam makes the dark pad and upright fascia
            // legible without placing decorative geometry across the instrument binnacle.
            AddRoundedBox(accent.mesh, Vector3(cabinHalf - 0.25f, 0.010f, 0.010f), 0.003f,
                          Matrix::CreateTranslation(0.5f * (cabinHalf + 0.25f), yFace + 0.125f, zFace + 0.025f));
            for (int k = 0; k < 3; ++k) {
                const float kx = -0.07f + 0.07f * static_cast<float>(k);
                gloss.mesh.AddCylinder(Vector3(kx, yFace - 0.05f, zStack - 0.14f * 0.5f + 0.06f), Vector3(0, 0, 1), 0.016f, 0.02f, 12, true);
                chrome.mesh.AddCylinder(Vector3(kx, yFace - 0.05f, zStack - 0.14f * 0.5f + 0.078f), Vector3(0, 0, 1), 0.006f, 0.004f, 8, true);
            }
            AddBoxTo(gloss, Vector3(0.0f, yFace - 0.10f, zStack + 0.055f), Vector3(0.20f, 0.03f, 0.008f));   // hazard/buttons strip
            // Glovebox lid seam on the passenger side (thin dark line quads).
            {
                const float gx0 = 0.22f, gx1 = cabinHalf - 0.06f;
                const float zg = zFace + 0.002f;
                const float yg0 = yFace - 0.15f, yg1 = yFace - 0.02f;
                gloss.mesh.AddQuad(Vector3(gx0, yg0, zg), Vector3(gx1, yg0, zg), Vector3(gx1, yg0 + 0.006f, zg), Vector3(gx0, yg0 + 0.006f, zg), Vector3(0, 0, 1),
                                   Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
                gloss.mesh.AddQuad(Vector3(gx0, yg1, zg), Vector3(gx1, yg1, zg), Vector3(gx1, yg1 + 0.006f, zg), Vector3(gx0, yg1 + 0.006f, zg), Vector3(0, 0, 1),
                                   Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
            }

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
                AddOrientedBox(gloss, c - n * 0.02f, Vector3(w + 0.025f, h + 0.025f, 0.04f), 0.0f, -tilt);   // bezel behind the face
                const Vector3 rimCentre = c + n * 0.006f;
                const float inset = 0.007f;
                const auto rimQuad = [&](const float x0, const float y0, const float x1, const float y1) {
                    accent.mesh.AddQuad(rimCentre + right * x0 + upv * y0, rimCentre + right * x1 + upv * y0,
                                        rimCentre + right * x1 + upv * y1, rimCentre + right * x0 + upv * y1,
                                        n, Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
                };
                rimQuad(-w * 0.5f - inset,  h * 0.5f, w * 0.5f + inset, h * 0.5f + inset);
                rimQuad(-w * 0.5f - inset, -h * 0.5f - inset, w * 0.5f + inset, -h * 0.5f);
                rimQuad(-w * 0.5f - inset, -h * 0.5f, -w * 0.5f, h * 0.5f);
                rimQuad( w * 0.5f, -h * 0.5f, w * 0.5f + inset, h * 0.5f);
                std::vector<std::vector<Vector3>> visor;
                // Crown the hood over the dials and taper its shoulders toward the
                // dashboard. A two-ring loft leaves a broad, flat slab in the windscreen.
                for (int j = -2; j <= 2; ++j) {
                    const float across = static_cast<float>(j) * 0.5f;
                    const float arch = 1.0f - std::fabs(across);
                    const float x = c.X + across * (w * 0.5f + 0.05f);
                    std::vector<Vector3> ring;
                    const Vector3 centre(x, c.Y + 0.01f + 0.012f * arch, c.Z + 0.02f);
                    const float R = (h * 0.5f + 0.035f) * (0.72f + 0.28f * arch);
                    for (int k = 0; k <= 10; ++k) {
                        const float a = kPi * 0.10f + (kPi * 0.90f) * static_cast<float>(k) / 10.0f;
                        ring.push_back(centre + upv * (std::sin(a) * R) + n * (std::cos(a) * R));
                    }
                    for (int k = 10; k >= 0; --k) {
                        const float a = kPi * 0.10f + (kPi * 0.90f) * static_cast<float>(k) / 10.0f;
                        ring.push_back(centre + upv * (std::sin(a) * (R - 0.014f)) + n * (std::cos(a) * (R - 0.014f)));
                    }
                    visor.push_back(std::move(ring));
                }
                MeshData shell;
                shell.AddLoft(visor, true);
                shell.OrientOutward();
                shell.ComputeSmoothNormals();
                interior.mesh.Append(shell, Matrix::getIdentityProperty());
            }
        }

        // ---- Steering wheel, column, stalks ------------------------------------------------
        CarPart steering = MakePart("steering_wheel", CarMaterial::InteriorMid, CarPart::Role::SteeringWheel);
        CarPart steeringTrim = MakePart("steering_spokes", CarMaterial::InteriorAccent, CarPart::Role::SteeringWheel);
        CarPart steeringBadge = MakePart("steering_badge", CarMaterial::Chrome, CarPart::Role::SteeringWheel);
        {
            const float tilt = Sim::Units::DegToRad(vis.steeringWheelTiltDeg);
            const Vector3 n(0.0f, std::sin(tilt), std::cos(tilt));
            steering.pivot = steeringTrim.pivot = steeringBadge.pivot = vis.steeringWheelCenter;
            steering.axis = steeringTrim.axis = steeringBadge.axis = n;
            const float R = vis.steeringWheelDiameterM * 0.5f;
            MeshData wheel;
            wheel.AddTorus(Vector3(0, 0, 0), Vector3(0, 0, 1), R, 0.019f, 44, 12);
            AddRoundedBox(wheel, Vector3(0.15f, 0.048f, 0.10f), 0.03f, Matrix::CreateRotationX(kPi * 0.5f) * Matrix::CreateTranslation(0.0f, -0.005f, 0.0f));
            MeshData spokeTrim;
            for (const float a : {0.0f, kPi, kPi * 1.5f}) {
                MeshData spoke;
                spoke.AddBox(Vector3(0.05f, -0.016f, -0.010f), Vector3(R - 0.008f, 0.016f, 0.012f), 1.0f);
                spoke.Transform(Matrix::CreateRotationZ(a));
                spokeTrim.Append(spoke, Matrix::getIdentityProperty());
            }
            wheel.Transform(Matrix::CreateRotationX(-tilt));
            wheel.ComputeSmoothNormals();
            steering.mesh = wheel;
            spokeTrim.Transform(Matrix::CreateRotationX(-tilt));
            spokeTrim.ComputeSmoothNormals();
            steeringTrim.mesh = spokeTrim;
            MeshData badge;
            badge.AddCylinder(Vector3(0, 0, 0.026f), Vector3(0, 0, 1), 0.018f, 0.004f, 16, true);
            badge.Transform(Matrix::CreateRotationX(-tilt));
            steeringBadge.mesh = badge;
            // Column shroud (tapered) and two stalks.
            std::vector<Vector2> shroud = {{0.030f, -0.03f}, {0.042f, -0.10f}, {0.056f, -0.28f}, {0.060f, -0.44f}, {0.0f, -0.44f}};
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
            MeshData brake;
            brake.AddBox(Vector3(-0.018f, -0.012f, -0.02f), Vector3(0.018f, 0.012f, 0.22f), 1.0f);
            brake.AddCylinder(Vector3(0.0f, 0.0f, 0.18f), Vector3(0, 0, 1), 0.015f, 0.06f, 10, true);
            brake.Transform(Matrix::CreateRotationX(0.28f) * Matrix::CreateTranslation(0.0f, consoleTop + 0.03f, 0.18f));
            interior.mesh.Append(brake, Matrix::getIdentityProperty());
            AddBoxTo(gloss, Vector3(0.0f, consoleTop + 0.015f, 0.20f), Vector3(0.06f, 0.03f, 0.08f));
            AddBoxTo(mid, Vector3(0.0f, consoleTop + 0.02f, 0.50f), Vector3(0.26f, 0.05f, 0.30f));   // rear of the console / armrest
        }

        // ---- Seats -------------------------------------------------------------------------
        // Front cushions centred 0.22 m behind the eye's z so the backrest sits ~0.15 m behind the head.
        const float zCushion = vis.driverEye.Z - 0.22f;
        AddSeat(fabric, interior, -0.37f, floorY, zCushion, 0.50f, false);
        AddSeat(fabric, interior, 0.37f, floorY, zCushion, 0.50f, false);
        {
            const float benchZ = 1.05f;
            AddRoundedBox(fabric.mesh, Vector3(2.0f * cabinHalf - 0.16f, 0.14f, 0.50f), 0.04f, Matrix::CreateTranslation(0.0f, floorY + 0.24f, benchZ));
            const Matrix backFrame = Matrix::CreateRotationX(0.30f) * Matrix::CreateTranslation(0.0f, floorY + 0.28f, benchZ + 0.22f);
            AddRoundedBox(fabric.mesh, Vector3(2.0f * cabinHalf - 0.18f, 0.60f, 0.10f), 0.035f, Matrix::CreateTranslation(0.0f, 0.30f, 0.0f) * backFrame);
            for (const float x : {-0.36f, 0.36f}) {
                AddRoundedBox(fabric.mesh, Vector3(0.24f, 0.14f, 0.08f), 0.03f, Matrix::CreateTranslation(x, 0.69f, 0.0f) * backFrame);
            }
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
            const float zArm = vis.driverEye.Z - 0.26f;   // elbow level, just ahead of the hip
            const float xDoor = cabinHalf + 0.03f;
            AddRoundedBox(mid.mesh, Vector3(0.09f, 0.05f, 0.34f), 0.02f, Matrix::CreateTranslation(side * (xDoor - 0.04f), belt - 0.24f, zArm));
            AddBoxTo(gloss, Vector3(side * (xDoor - 0.06f), belt - 0.21f, zArm - 0.06f), Vector3(0.05f, 0.006f, 0.10f));
            AddRoundedBox(chrome.mesh, Vector3(0.02f, 0.03f, 0.11f), 0.008f, Matrix::CreateTranslation(side * (xDoor - 0.05f), belt - 0.12f, zArm - 0.30f));
        }

        // ---- Interior mirror and sun visors -------------------------------------------------
        {
            const Vector3 c = vis.mirrorCenter;
            AddRoundedBox(interior.mesh, Vector3(0.25f, 0.075f, 0.028f), 0.012f, Matrix::CreateTranslation(c + Vector3(0.0f, 0.0f, -0.016f)));
            // Stem from the housing top to the inside of the windshield just ahead of it.
            {
                const float zMount = c.Z - 0.06f;
                float yGlass = c.Y + 0.12f;
                float best = 1e9f;
                for (std::size_t r = 0; r < skin.stations.size(); ++r) {
                    const float d = std::fabs(skin.stations[r] - zMount);
                    if (d < best) { best = d; yGlass = skin.rings[r][Ring::kTop].Y; }
                }
                const Vector3 top(0.0f, yGlass - 0.012f, zMount);
                const Vector3 base = c + Vector3(0.0f, 0.03f, -0.01f);
                const Vector3 d = top - base;
                const float len = d.Length();
                if (len > 0.02f) {
                    AddOrientedBox(interior, (top + base) * 0.5f, Vector3(0.022f, len, 0.022f), 0.0f, std::atan2(d.Z, d.Y));
                }
            }
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

        interior.cabin = mid.cabin = accent.cabin = light.cabin = fabric.cabin = dashSoft.cabin = steering.cabin = steeringTrim.cabin = true;
        for (CarPart* p : {&interior, &mid, &accent, &light, &fabric, &dashSoft, &gloss, &vents, &chrome, &cluster, &steering, &steeringTrim, &steeringBadge, &gearLever, &mirror}) {
            if (p->mesh.TriangleCount() > 0) model.parts.push_back(std::move(*p));
        }
    }

    void BuildCabinBlock(CarModel& model, const CarStyle& style, const SkinGrid& skin, const std::vector<std::vector<CarMaterial>>& skinMaterials,
                         const float zCowl, const float zSideGlassRear)
    {
        const float floorY = style.rideHeight + 0.05f;
        const float belt = style.beltHeight;
        const float cabinHalf = style.width * 0.5f - 0.11f;
        const float zR = style.RearZ();
        CarPart cabin = MakePart("cabin", CarMaterial::Interior, CarPart::Role::Interior);
        cabin.cabin = true;
        CopyInnerShell(skin, skinMaterials, zCowl - 0.03f, zR - 0.03f, [&](int rs, float zc, float) -> CarPart* {
            if (rs >= Ring::kGlassBase) return &cabin;
            if (rs >= Ring::kRockerTop && zc > zCowl + 0.05f) return &cabin;
            return nullptr;
        });
        (void)zSideGlassRear;
        // Floor, dashboard slab, front seats and rear bench as simple blocks.
        AddBoxTo(cabin, Vector3(0.0f, floorY, 0.5f * (zCowl + zR - 0.3f)), Vector3(2.0f * cabinHalf + 0.1f, 0.03f, (zR - 0.3f) - zCowl));
        const float dashTop = belt + 0.02f;
        AddBoxTo(cabin, Vector3(0.0f, 0.5f * (dashTop + floorY + 0.25f), zCowl + 0.16f), Vector3(2.0f * cabinHalf + 0.1f, dashTop - floorY - 0.25f, 0.32f));
        for (const float x : {-0.37f, 0.37f}) {
            AddRoundedBox(cabin.mesh, Vector3(0.50f, 0.14f, 0.50f), 0.04f, Matrix::CreateTranslation(x, floorY + 0.24f, 0.30f));
            AddRoundedBox(cabin.mesh, Vector3(0.48f, 0.66f, 0.12f), 0.04f, Matrix::CreateRotationX(0.34f) * Matrix::CreateTranslation(x, floorY + 0.58f, 0.62f));
            AddRoundedBox(cabin.mesh, Vector3(0.26f, 0.15f, 0.09f), 0.03f, Matrix::CreateTranslation(x, floorY + 0.98f, 0.72f));
        }
        const float benchZ = std::min(1.05f, zR - 0.9f);
        AddRoundedBox(cabin.mesh, Vector3(2.0f * cabinHalf - 0.16f, 0.14f, 0.50f), 0.04f, Matrix::CreateTranslation(0.0f, floorY + 0.24f, benchZ));
        AddRoundedBox(cabin.mesh, Vector3(2.0f * cabinHalf - 0.18f, 0.60f, 0.10f), 0.035f,
                      Matrix::CreateRotationX(0.30f) * Matrix::CreateTranslation(0.0f, floorY + 0.58f, benchZ + 0.25f));
        // Steering wheel silhouette on the left.
        MeshData wheel;
        wheel.AddTorus(Vector3(0, 0, 0), Vector3(0, 0.45f, 0.89f), 0.18f, 0.018f, 24, 8);
        wheel.Transform(Matrix::CreateTranslation(-0.37f, floorY + 0.62f, zCowl + 0.45f));
        cabin.mesh.Append(wheel, Matrix::getIdentityProperty());
        if (cabin.mesh.TriangleCount() > 0) model.parts.push_back(std::move(cabin));
    }
}
