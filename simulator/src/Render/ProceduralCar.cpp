#include "CarSim/Render/ProceduralCar.hpp"

#include "CarSim/Sim/Units.hpp"

#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Render
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    // ------------------------------------------------------------------ profile

    HatchbackProfile::HatchbackProfile(const Sim::VehicleDefinition& d)
        : length(d.chassis.lengthM),
          width(d.chassis.widthM),
          height(d.chassis.heightM),
          wheelbase(std::max(2.0f, d.WheelbaseM()))
    {
        const float overhang = std::max(0.9f, length - wheelbase);
        frontOverhang = overhang * 0.55f;
        rearOverhang = overhang * 0.45f;
        sillHeight = 0.19f;
        beltline = height * 0.585f;
        hoodHeight = height * 0.52f;
        roofFront = -wheelbase * 0.5f + wheelbase * 0.36f;     // A-pillar top
        roofRear = wheelbase * 0.5f + rearOverhang * 0.05f;    // hatch top
    }

    namespace
    {
        float SmoothStep(const float a, const float b, const float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    float HatchbackProfile::TopHeight(const float z) const
    {
        const float frontZ = FrontZ();
        const float rearZ = RearZ();
        const float cowl = roofFront - 0.62f;                 // windshield base
        const float noseTop = height * 0.44f;
        if (z <= cowl) {
            // Nose rises to the hood, hood rises gently to the cowl.
            const float t = SmoothStep(frontZ, frontZ + 0.35f, z);
            const float hoodBase = noseTop + (hoodHeight - noseTop) * t;
            const float slope = (hoodHeight + 0.045f - hoodBase) * SmoothStep(frontZ + 0.35f, cowl, z);
            return hoodBase + slope;
        }
        if (z <= roofFront) {
            // Windshield: smooth rake from the cowl to the roof.
            const float t = SmoothStep(cowl, roofFront, z);
            return (hoodHeight + 0.045f) + (height - (hoodHeight + 0.045f)) * std::sin(t * std::numbers::pi_v<float> * 0.5f);
        }
        if (z <= roofRear) {
            // Roof: very slight fall towards the rear.
            const float t = (z - roofFront) / std::max(0.1f, roofRear - roofFront);
            return height - 0.025f * t;
        }
        // Tailgate: steep window then a rounded lip down to the bumper top.
        const float t = SmoothStep(roofRear, rearZ, z);
        const float tailTop = height * 0.50f;
        return height - 0.025f - (height - 0.025f - tailTop) * std::pow(t, 0.75f);
    }

    float HatchbackProfile::HalfWidth(const float z, const float y) const
    {
        const float frontZ = FrontZ();
        const float rearZ = RearZ();
        // Plan view: full width between the axles, tapering to the nose and tail.
        float plan = width * 0.5f;
        const float noseTaper = SmoothStep(frontZ + 0.9f, frontZ, z);
        const float tailTaper = SmoothStep(rearZ - 0.6f, rearZ, z);
        plan *= 1.0f - 0.16f * noseTaper - 0.10f * tailTaper;
        // Shoulder is the widest point (belt line); the sill tucks in; the greenhouse leans in.
        const float top = TopHeight(z);
        if (y <= beltline) {
            const float t = std::clamp((y - sillHeight) / std::max(0.05f, beltline - sillHeight), 0.0f, 1.0f);
            return plan * (0.90f + 0.10f * std::sin(t * std::numbers::pi_v<float> * 0.5f));
        }
        const float span = std::max(0.02f, top - beltline);
        const float t = std::clamp((y - beltline) / span, 0.0f, 1.0f);
        const float tumblehome = std::pow(t, 1.35f);
        // Above the hood line the taper only applies where there is a greenhouse.
        const float cowl = roofFront - 0.62f;
        const float greenhouse = z > cowl ? 1.0f : 0.0f;
        return plan * (1.0f - (0.34f * greenhouse + 0.06f * (1.0f - greenhouse)) * tumblehome);
    }

    bool HatchbackProfile::IsGlass(const float z, const float y) const
    {
        const float cowl = roofFront - 0.62f;
        if (z < cowl + 0.02f || y < beltline + 0.03f) {
            return false;
        }
        if (z > RearZ() - 0.35f) {
            return false;   // tailgate metal
        }
        return true;
    }

    // ------------------------------------------------------------------ generator

    namespace
    {
        struct Outline
        {
            std::vector<Vector2> right;   // (x, y) from the floor centre to the top centre
        };

        constexpr int kPointsPerHalf = 18;

        Outline SectionOutline(const HatchbackProfile& p, const float z, const Sim::VehicleDefinition& def)
        {
            Outline o;
            const float top = p.TopHeight(z);
            const float floorY = p.sillHeight;
            // Wheel arch: the lower edge of the side follows an arc around the nearest wheel.
            float archTop = floorY;
            const float archRadius = def.wheels.front().radiusM + 0.075f;
            for (const auto& w : def.wheels) {
                const float dz = z - w.position.Z;
                if (std::fabs(dz) < archRadius) {
                    archTop = std::max(archTop, w.position.Y + std::sqrt(archRadius * archRadius - dz * dz));
                }
            }
            const bool inArch = archTop > floorY + 0.005f;
            const float hwSill = p.HalfWidth(z, floorY + 0.02f);
            const float hwBelt = p.HalfWidth(z, p.beltline);
            const float xInner = inArch ? hwSill - 0.16f : hwSill * 0.98f;
            const float yLip = inArch ? archTop + 0.015f : floorY + 0.015f;

            // S1 floor: centre -> inner edge (2)
            o.right.emplace_back(0.0f, floorY);
            o.right.emplace_back(xInner, floorY);
            // S2 liner: up to the arch (3)
            for (int i = 1; i <= 3; ++i) {
                const float t = static_cast<float>(i) / 3.0f;
                o.right.emplace_back(xInner + (inArch ? 0.0f : 0.005f * t), floorY + (archTop - floorY) * t);
            }
            // S3 arch lip: out to the body side (1)
            o.right.emplace_back(hwSill, yLip);
            // S4 lower side up to the shoulder, slightly convex (4)
            for (int i = 1; i <= 4; ++i) {
                const float t = static_cast<float>(i) / 4.0f;
                const float y = yLip + (p.beltline - yLip) * t;
                const float bulge = std::sin(t * std::numbers::pi_v<float>) * 0.012f;
                o.right.emplace_back(p.HalfWidth(z, y) + bulge, y);
            }
            // S5 greenhouse / hood top side (4). The last step is a thin band so that the final
            // segment can be classified as the painted roof rail above the side glass.
            const float topSide = std::max(p.beltline + 0.005f, top - 0.045f);
            for (const float t : {0.32f, 0.64f, 0.92f, 1.0f}) {
                const float y = p.beltline + (topSide - p.beltline) * t;
                o.right.emplace_back(p.HalfWidth(z, y), y);
            }
            // S6 roof: rounded corner to the centre (4)
            const float hwTop = p.HalfWidth(z, topSide);
            for (int i = 1; i <= 4; ++i) {
                const float t = static_cast<float>(i) / 4.0f;
                const float a = t * std::numbers::pi_v<float> * 0.5f;
                const float x = hwTop * std::cos(a);
                const float y = topSide + (top - topSide) * std::sin(a);
                o.right.emplace_back(x, y);
            }
            (void)hwBelt;
            return o;
        }

        std::vector<Vector3> RingFromOutline(const Outline& o, const float z)
        {
            std::vector<Vector3> ring;
            ring.reserve(o.right.size() * 2 - 2);
            for (const auto& p : o.right) {
                ring.emplace_back(p.X, p.Y, z);
            }
            for (int i = static_cast<int>(o.right.size()) - 2; i >= 1; --i) {
                ring.emplace_back(-o.right[static_cast<std::size_t>(i)].X, o.right[static_cast<std::size_t>(i)].Y, z);
            }
            return ring;
        }

        /// Longitudinal positions of the material boundaries along the body (all in body space).
        struct BodyZones
        {
            float cowl;            // windshield base
            float sideGlassFront;  // front edge of the door glass (behind the A-pillar)
            float bPillarFront;
            float bPillarRear;
            float sideGlassRear;   // rear edge of the quarter glass (C-pillar)
            float rearWindowTop;   // top of the tailgate window (= roofRear)
            float rearWindowBottom;
            float frontBumper;     // rear edge of the front bumper's black lip
            float rearBumper;      // front edge of the rear bumper's black lip

            explicit BodyZones(const HatchbackProfile& p)
                : cowl(p.roofFront - 0.62f),
                  sideGlassFront(p.roofFront - 0.62f + 0.10f),   // just behind the cowl: slim A-pillar, quarter-glass triangle
                  bPillarFront(p.roofFront + (p.roofRear - p.roofFront) * 0.52f - 0.07f),
                  bPillarRear(p.roofFront + (p.roofRear - p.roofFront) * 0.52f + 0.07f),
                  sideGlassRear(p.roofRear - 0.20f),
                  rearWindowTop(p.roofRear + 0.02f),
                  rearWindowBottom(p.RearZ() - 0.36f),
                  frontBumper(p.FrontZ() + 0.42f),
                  rearBumper(p.RearZ() - 0.40f)
            {
            }
        };

        std::vector<float> Stations(const HatchbackProfile& p, const BodyZones& zones)
        {
            std::vector<float> zs;
            const float frontZ = p.FrontZ();
            const float rearZ = p.RearZ();
            // Dense sampling everywhere the surface curves: 6 cm steps give clean arches and rake.
            for (float z = frontZ; z < rearZ - 0.001f; z += 0.06f) {
                zs.push_back(z);
            }
            zs.push_back(rearZ);
            // Exact stations at the material boundaries so that glass, pillars and trim have
            // straight edges instead of a stair-step along the sampling grid.
            for (const float boundary : {zones.cowl, zones.sideGlassFront, zones.bPillarFront, zones.bPillarRear,
                                         zones.sideGlassRear, p.roofFront, zones.rearWindowTop, zones.rearWindowBottom,
                                         zones.frontBumper, zones.rearBumper}) {
                if (boundary <= frontZ + 0.02f || boundary >= rearZ - 0.02f) {
                    continue;
                }
                bool merged = false;
                for (float& z : zs) {
                    if (std::fabs(z - boundary) < 0.02f && z > frontZ + 0.01f && z < rearZ - 0.01f) {
                        z = boundary;   // snap the nearest regular station onto the boundary
                        merged = true;
                        break;
                    }
                }
                if (!merged) {
                    zs.push_back(boundary);
                }
            }
            std::sort(zs.begin(), zs.end());
            return zs;
        }

        /// Outline segment index (0..16 on the right half) for a ring segment index of the closed ring.
        int OutlineSegment(const int ringSegment)
        {
            return ringSegment <= 16 ? ringSegment : 33 - ringSegment;
        }

        void AddCap(MeshData& mesh, const std::vector<Vector3>& ring, const Vector3& normal, const Color& color)
        {
            Vector3 center(0.0f, 0.0f, 0.0f);
            for (const auto& p : ring) {
                center = center + p;
            }
            center = center * (1.0f / static_cast<float>(ring.size()));
            const std::uint32_t c = mesh.AddVertex(center, normal, Vector2(0.5f, 0.5f), color);
            std::vector<std::uint32_t> ids;
            for (const auto& p : ring) {
                ids.push_back(mesh.AddVertex(p, normal, Vector2(0.5f, 0.5f), color));
            }
            const bool front = normal.Z < 0.0f;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const std::uint32_t a = ids[i];
                const std::uint32_t b = ids[(i + 1) % ring.size()];
                if (front) {
                    mesh.AddTriangle(c, b, a);
                } else {
                    mesh.AddTriangle(c, a, b);
                }
            }
        }

        CarPart MakePart(const std::string& name, CarMaterial material, CarPart::Role role = CarPart::Role::Static)
        {
            CarPart part;
            part.name = name;
            part.material = material;
            part.role = role;
            return part;
        }

        void AddBoxTo(CarPart& part, const Vector3& center, const Vector3& size)
        {
            part.mesh.AddBox(center - size * 0.5f, center + size * 0.5f, 1.0f);
        }

        /// Box rotated about Y by `yaw` and about X by `pitch`, then translated.
        void AddOrientedBox(CarPart& part, const Vector3& center, const Vector3& size, float yaw, float pitch)
        {
            MeshData box;
            box.AddBox(size * -0.5f, size * 0.5f, 1.0f);
            box.Transform(Matrix::CreateRotationX(pitch) * Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(center));
            part.mesh.Append(box, Matrix::getIdentityProperty());
        }
    }

    CarModel GenerateCar(const Sim::VehicleDefinition& def)
    {
        CarModel model;
        const HatchbackProfile profile(def);
        const float frontZ = profile.FrontZ();
        const float rearZ = profile.RearZ();
        const BodyZones zones(profile);
        const float cowl = zones.cowl;
        const float belt = profile.beltline;
        model.wheelRadius = def.wheels.front().radiusM;

        // ---- Body loft ------------------------------------------------------------------
        std::vector<std::vector<Vector3>> rings;
        const std::vector<float> stations = Stations(profile, zones);
        for (const float z : stations) {
            rings.push_back(RingFromOutline(SectionOutline(profile, z, def), z));
        }
        // Slightly shrink the very first and last rings towards their centre for rounded ends.
        const auto shrink = [](std::vector<Vector3>& ring, float factorX, float factorY) {
            Vector3 c(0, 0, 0);
            for (const auto& p : ring) c = c + p;
            c = c * (1.0f / static_cast<float>(ring.size()));
            for (auto& p : ring) {
                p = Vector3(c.X + (p.X - c.X) * factorX, c.Y + (p.Y - c.Y) * factorY, p.Z);
            }
        };
        shrink(rings.front(), 0.86f, 0.80f);
        shrink(rings.back(), 0.92f, 0.86f);

        MeshData loft;
        loft.AddLoft(rings, true);
        loft.ComputeSmoothNormals();

        CarPart paint = MakePart("body_paint", CarMaterial::Paint);
        CarPart glass = MakePart("body_glass", CarMaterial::Glass);
        CarPart trim = MakePart("body_trim", CarMaterial::BlackTrim);
        CarPart shell = MakePart("interior_shell", CarMaterial::InteriorLight, CarPart::Role::Interior);
        CarPart doorCards = MakePart("interior_doors", CarMaterial::Interior, CarPart::Role::Interior);

        // Every loft quad is classified by its station (z) and by which outline segment it
        // belongs to, so material edges follow the mesh topology exactly. Outline segments on
        // the right half: 0 floor, 1-3 arch liner, 4 arch lip, 5-8 lower side (5 = sill band),
        // 9-11 side glass band, 12 roof rail, 13-16 roof corner (13 = pillar edge).
        const float archRadius = def.wheels.front().radiusM + 0.075f;
        const float frontArchRear = def.wheels.front().position.Z + archRadius;
        const float rearArchFront = def.wheels.back().position.Z - archRadius;
        const auto classify = [&](const float z, const int segment) -> CarMaterial {
            if (segment <= 4) {
                return CarMaterial::BlackTrim;   // underbody, liners and arch lips
            }
            if (segment == 5) {
                const bool sill = z > frontArchRear && z < rearArchFront;
                const bool bumperLip = z < zones.frontBumper || z > zones.rearBumper;
                return (sill || bumperLip) ? CarMaterial::BlackTrim : CarMaterial::Paint;
            }
            if (segment <= 8) {
                return CarMaterial::Paint;       // doors, fenders and quarter panels
            }
            if (segment <= 11) {
                // Side glass band: door and quarter glass between the pillars.
                const bool frontDoor = z > zones.sideGlassFront && z < zones.bPillarFront;
                const bool rearDoor = z > zones.bPillarRear && z < zones.sideGlassRear;
                return (frontDoor || rearDoor) ? CarMaterial::Glass : CarMaterial::Paint;
            }
            if (segment == 12) {
                return CarMaterial::Paint;       // roof rail / drip edge
            }
            // Roof corner segments: windshield and rear window are glass except the pillar edge.
            const bool windshield = z > cowl && z < profile.roofFront;
            const bool rearWindow = z > zones.rearWindowTop && z < zones.rearWindowBottom;
            if ((windshield || rearWindow) && segment >= 14) {
                return CarMaterial::Glass;
            }
            return CarMaterial::Paint;
        };

        const auto copyTriangle = [&](MeshData& dst, std::size_t t, const Color& color, bool flip) {
            std::uint32_t ids[3];
            for (int k = 0; k < 3; ++k) {
                MeshVertex v = loft.vertices[loft.indices[t * 3 + static_cast<std::size_t>(k)]];
                v.color = color;
                if (flip) {
                    v.normal = -v.normal;
                }
                ids[k] = dst.AddVertex(v);
            }
            // Emitted order is already XNA front-facing; re-add raw to preserve it.
            if (!flip) {
                dst.indices.push_back(ids[0]);
                dst.indices.push_back(ids[1]);
                dst.indices.push_back(ids[2]);
            } else {
                dst.indices.push_back(ids[0]);
                dst.indices.push_back(ids[2]);
                dst.indices.push_back(ids[1]);
            }
        };

        const Color white(255, 255, 255, 255);
        const std::size_t ringSize = rings.front().size();   // 34 vertices, 34 segments (closed ring)
        for (std::size_t t = 0; t < loft.TriangleCount(); ++t) {
            const std::size_t quad = t / 2;
            const std::size_t station = quad / ringSize;
            const int segment = OutlineSegment(static_cast<int>(quad % ringSize));
            const float z = 0.5f * (stations[station] + stations[station + 1]);
            const CarMaterial material = classify(z, segment);
            switch (material) {
                case CarMaterial::Glass: copyTriangle(glass.mesh, t, white, false); break;
                case CarMaterial::BlackTrim: copyTriangle(trim.mesh, t, white, false); break;
                default: copyTriangle(paint.mesh, t, white, false); break;
            }
            // Interior shell: the cabin's inner surfaces (headliner, pillars, tailgate) and door cards.
            const bool cabin = z > cowl - 0.2f && z < rearZ - 0.05f;
            if (cabin && material != CarMaterial::Glass) {
                if (segment >= 9) {
                    copyTriangle(shell.mesh, t, white, true);
                } else if (segment >= 5) {
                    copyTriangle(doorCards.mesh, t, white, true);
                }
            }
        }
        AddCap(paint.mesh, rings.front(), Vector3(0, 0, -1), white);
        AddCap(paint.mesh, rings.back(), Vector3(0, 0, 1), white);

        // ---- Front and rear details -----------------------------------------------------
        const float hwFront = profile.HalfWidth(frontZ + 0.12f, 0.55f);
        const float hwRear = profile.HalfWidth(rearZ - 0.12f, 0.85f);
        CarPart lampHead = MakePart("lamp_head", CarMaterial::LampHead);
        CarPart lampTail = MakePart("lamp_tail", CarMaterial::LampTail);
        CarPart lampReverse = MakePart("lamp_reverse", CarMaterial::LampReverse);
        CarPart indLF = MakePart("indicator_left_front", CarMaterial::LampIndicator);
        CarPart indRF = MakePart("indicator_right_front", CarMaterial::LampIndicator);
        CarPart indLR = MakePart("indicator_left_rear", CarMaterial::LampIndicator);
        CarPart indRR = MakePart("indicator_right_rear", CarMaterial::LampIndicator);
        CarPart chrome = MakePart("chrome", CarMaterial::Chrome);
        CarPart grille = MakePart("grille", CarMaterial::BlackTrim);
        CarPart plates = MakePart("plates", CarMaterial::Plate);

        const float headY = profile.TopHeight(frontZ + 0.10f) - 0.09f;
        for (const float side : {-1.0f, 1.0f}) {
            // Headlights: wrap-around lens pieces on the nose corners.
            AddOrientedBox(lampHead, Vector3(side * (hwFront - 0.20f), headY, frontZ + 0.03f), Vector3(0.40f, 0.14f, 0.10f), side * 0.35f, -0.25f);
            // Front indicators just outboard/below the headlights.
            AddOrientedBox(side < 0 ? indLF : indRF, Vector3(side * (hwFront - 0.05f), headY - 0.02f, frontZ + 0.12f), Vector3(0.08f, 0.06f, 0.10f), side * 0.8f, 0.0f);
            // Tail lamps: vertical units on the rear corners with a reverse segment.
            const float tailY = profile.TopHeight(rearZ - 0.05f) - 0.05f;
            AddOrientedBox(lampTail, Vector3(side * (hwRear - 0.12f), tailY, rearZ - 0.03f), Vector3(0.22f, 0.20f, 0.08f), side * -0.25f, 0.0f);
            AddOrientedBox(side < 0 ? indLR : indRR, Vector3(side * (hwRear - 0.12f), tailY - 0.13f, rearZ - 0.03f), Vector3(0.22f, 0.05f, 0.08f), side * -0.25f, 0.0f);
            AddOrientedBox(lampReverse, Vector3(side * (hwRear - 0.12f), tailY - 0.19f, rearZ - 0.03f), Vector3(0.22f, 0.05f, 0.08f), side * -0.25f, 0.0f);
            // Side mirrors: housing + mirror glass.
            const float mirrorZ = cowl + 0.18f;
            const float mirrorY = belt + 0.06f;
            const float mirrorX = side * (profile.HalfWidth(mirrorZ, mirrorY) + 0.09f);
            AddBoxTo(trim, Vector3(mirrorX, mirrorY, mirrorZ), Vector3(0.17f, 0.10f, 0.08f));
            AddBoxTo(trim, Vector3(side * (profile.HalfWidth(mirrorZ, mirrorY) + 0.02f), mirrorY - 0.02f, mirrorZ), Vector3(0.05f, 0.02f, 0.03f));
            // Door handles.
            AddBoxTo(chrome, Vector3(side * (profile.HalfWidth(0.05f, belt - 0.12f) + 0.012f), belt - 0.12f, 0.05f), Vector3(0.02f, 0.03f, 0.16f));
            AddBoxTo(chrome, Vector3(side * (profile.HalfWidth(0.95f, belt - 0.12f) + 0.012f), belt - 0.12f, 0.95f), Vector3(0.02f, 0.03f, 0.16f));
        }
        // Grille and lower air intake.
        AddOrientedBox(grille, Vector3(0.0f, headY - 0.01f, frontZ + 0.015f), Vector3(0.62f, 0.10f, 0.05f), 0.0f, -0.25f);
        AddBoxTo(grille, Vector3(0.0f, profile.sillHeight + 0.19f, frontZ + 0.02f), Vector3(0.9f, 0.14f, 0.05f));
        // Chrome badge strip and exhaust.
        AddBoxTo(chrome, Vector3(0.0f, headY + 0.02f, frontZ + 0.01f), Vector3(0.10f, 0.03f, 0.03f));
        chrome.mesh.AddCylinder(Vector3(0.35f, profile.sillHeight + 0.02f, rearZ - 0.15f), Vector3(0, 0, 1), 0.025f, 0.18f, 10, true);

        // Registration plates: 520 x 110 mm faces.
        const float plateW = 0.52f;
        const float plateH = 0.11f;
        model.frontPlateCenter = Vector3(0.0f, profile.sillHeight + 0.33f, frontZ - 0.002f);
        model.rearPlateCenter = Vector3(0.0f, profile.sillHeight + 0.52f, rearZ + 0.002f);
        plates.mesh.AddQuad(model.frontPlateCenter + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0), model.frontPlateCenter + Vector3(-plateW * 0.5f, plateH * 0.5f, 0),
                            model.frontPlateCenter + Vector3(plateW * 0.5f, plateH * 0.5f, 0), model.frontPlateCenter + Vector3(plateW * 0.5f, -plateH * 0.5f, 0),
                            Vector3(0, 0, -1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
        plates.mesh.AddQuad(model.rearPlateCenter + Vector3(plateW * 0.5f, -plateH * 0.5f, 0), model.rearPlateCenter + Vector3(plateW * 0.5f, plateH * 0.5f, 0),
                            model.rearPlateCenter + Vector3(-plateW * 0.5f, plateH * 0.5f, 0), model.rearPlateCenter + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0),
                            Vector3(0, 0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
        // Plate backing boxes.
        AddBoxTo(trim, model.frontPlateCenter + Vector3(0, 0, 0.015f), Vector3(plateW + 0.02f, plateH + 0.02f, 0.025f));
        AddBoxTo(trim, model.rearPlateCenter - Vector3(0, 0, 0.015f), Vector3(plateW + 0.02f, plateH + 0.02f, 0.025f));

        // ---- Wheels and arch liners -----------------------------------------------------
        const CarPart::Role wheelRoles[4] = {CarPart::Role::WheelFL, CarPart::Role::WheelFR, CarPart::Role::WheelRL, CarPart::Role::WheelRR};
        for (std::size_t i = 0; i < def.wheels.size() && i < 4; ++i) {
            const auto& w = def.wheels[i];
            const float side = w.position.X < 0.0f ? -1.0f : 1.0f;
            CarPart tyre = MakePart("tyre_" + w.name, CarMaterial::Tyre, wheelRoles[i]);
            CarPart rim = MakePart("rim_" + w.name, CarMaterial::Rim, wheelRoles[i]);
            tyre.pivot = w.position;
            rim.pivot = w.position;
            tyre.axis = Vector3(1, 0, 0);
            rim.axis = Vector3(1, 0, 0);
            // Tyre: torus-like tread approximated by a wide torus plus a cylinder tread band.
            const float r = w.radiusM;
            const float half = w.widthM * 0.5f;
            tyre.mesh.AddTorus(Vector3(0, 0, 0), Vector3(1, 0, 0), r - half * 0.55f, half * 0.55f, 32, 10);
            tyre.mesh.AddCylinder(Vector3(-half * 0.55f, 0, 0), Vector3(1, 0, 0), r - 0.004f, half * 1.1f, 32, false);
            // Rim: dish with five spokes.
            const float rimR = r - half * 0.55f - 0.01f;
            rim.mesh.AddCylinder(Vector3(-half * 0.35f, 0, 0), Vector3(1, 0, 0), rimR, half * 0.70f, 24, true);
            for (int s = 0; s < 5; ++s) {
                const float a = static_cast<float>(s) / 5.0f * 2.0f * std::numbers::pi_v<float>;
                MeshData spoke;
                spoke.AddBox(Vector3(half * 0.30f, -0.018f, 0.0f), Vector3(half * 0.42f, 0.018f, rimR * 0.92f), 1.0f);
                spoke.Transform(Matrix::CreateRotationX(a));
                rim.mesh.Append(spoke, Matrix::getIdentityProperty());
            }
            rim.mesh.AddCylinder(Vector3(half * 0.30f, 0, 0), Vector3(1, 0, 0), 0.05f, half * 0.16f, 12, true);
            // Mirror the wheel for the left side so the rim face points outwards.
            if (side < 0.0f) {
                tyre.mesh.Transform(Matrix::CreateScale(-1.0f, 1.0f, 1.0f));
                rim.mesh.Transform(Matrix::CreateScale(-1.0f, 1.0f, 1.0f));
                tyre.mesh.FlipWinding();
                rim.mesh.FlipWinding();
                tyre.mesh.ComputeSmoothNormals();
                rim.mesh.ComputeSmoothNormals();
            }
            model.parts.push_back(std::move(tyre));
            model.parts.push_back(std::move(rim));
            // Arch liner: dark half-shell above the wheel, inside the body.
            CarPart liner = MakePart("arch_liner_" + w.name, CarMaterial::BlackTrim);
            const float archR = r + 0.06f;
            std::vector<std::vector<Vector3>> linerRings;
            for (int k = 0; k <= 12; ++k) {
                const float a = static_cast<float>(k) / 12.0f * std::numbers::pi_v<float>;
                const float y = w.position.Y + std::sin(a) * archR;
                const float z = w.position.Z + std::cos(a) * archR;
                const float xIn = side * (std::fabs(w.position.X) - half - 0.02f);
                const float xOut = side * (profile.HalfWidth(z, std::min(y, belt)) - 0.005f);
                linerRings.push_back({Vector3(xIn, y, z), Vector3(xOut, y, z)});
            }
            liner.mesh.AddLoft(linerRings, false);
            liner.mesh.ComputeSmoothNormals();
            // The liner faces inwards (towards the wheel): flip if its normals point up.
            if (!liner.mesh.vertices.empty() && liner.mesh.vertices[1].normal.Y > 0.0f) {
                liner.mesh.FlipWinding();
            }
            model.parts.push_back(std::move(liner));
        }

        // ---- Interior --------------------------------------------------------------------
        const auto& vis = def.visual;
        CarPart interior = MakePart("interior", CarMaterial::Interior, CarPart::Role::Interior);
        CarPart interiorLight = MakePart("interior_light", CarMaterial::InteriorLight, CarPart::Role::Interior);
        const float cabinHalf = profile.HalfWidth(0.3f, belt - 0.2f) - 0.06f;
        const float floorY = profile.sillHeight + 0.04f;
        // Floor and firewall.
        AddBoxTo(interior, Vector3(0.0f, floorY, 0.55f), Vector3(cabinHalf * 2.0f, 0.03f, 2.6f));
        AddBoxTo(interior, Vector3(0.0f, (floorY + belt) * 0.5f, cowl + 0.05f), Vector3(cabinHalf * 2.0f, belt - floorY, 0.04f));
        // Dashboard: a sloped slab from the cowl towards the occupants, with a top pad.
        {
            std::vector<std::vector<Vector3>> dash;
            const float zBack = cowl + 0.58f;
            const float yTop = belt + 0.11f;
            const float xr = cabinHalf + 0.01f;
            dash.push_back({Vector3(-xr, belt - 0.02f, cowl + 0.02f), Vector3(-xr, yTop - 0.02f, cowl + 0.05f), Vector3(-xr, yTop, cowl + 0.30f), Vector3(-xr, yTop - 0.12f, zBack), Vector3(-xr, floorY + 0.25f, zBack + 0.05f), Vector3(-xr, floorY + 0.25f, cowl + 0.02f)});
            dash.push_back({Vector3(xr, belt - 0.02f, cowl + 0.02f), Vector3(xr, yTop - 0.02f, cowl + 0.05f), Vector3(xr, yTop, cowl + 0.30f), Vector3(xr, yTop - 0.12f, zBack), Vector3(xr, floorY + 0.25f, zBack + 0.05f), Vector3(xr, floorY + 0.25f, cowl + 0.02f)});
            MeshData slab;
            slab.AddLoft(dash, true);
            slab.ComputeSmoothNormals();
            // Loft of two rings across x: ensure it faces outwards (check the top face normal).
            bool up = false;
            for (std::size_t t = 0; t < slab.TriangleCount(); ++t) {
                const Vector3 n = -slab.EmittedTriangleNormal(t);
                if (n.Y > 0.5f) { up = true; break; }
            }
            if (!up) slab.FlipWinding();
            slab.MakeFlatShaded();
            interior.mesh.Append(slab, Matrix::getIdentityProperty());
            // Centre console.
            AddBoxTo(interior, Vector3(0.0f, floorY + 0.16f, zBack + 0.35f), Vector3(0.28f, 0.30f, 0.80f));
        }
        // Instrument cluster housing (binnacle) in front of the driver.
        CarPart cluster = MakePart("cluster", CarMaterial::Cluster, CarPart::Role::Interior);
        {
            const Vector3 c = vis.clusterCenter;
            AddOrientedBox(interior, c + Vector3(0.0f, 0.02f, -0.06f), Vector3(0.36f, 0.16f, 0.12f), 0.0f, -0.35f);
            // Cluster face: a quad tilted back 20 degrees, facing the driver (+z).
            const float tilt = 0.35f;
            const Vector3 right(0.17f, 0.0f, 0.0f);
            const Vector3 upv(0.0f, 0.075f * std::cos(tilt), -0.075f * std::sin(tilt));
            const Vector3 n(0.0f, std::sin(tilt), std::cos(tilt));
            const Vector3 o = c + n * 0.001f;
            cluster.mesh.AddQuad(o - right - upv, o + right - upv, o + right + upv, o - right + upv, n,
                                 Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
            // Needles: thin boxes pivoting at the gauge centres (speedometer left, tachometer right).
            const auto needle = [&](const std::string& name, CarPart::Role role, const Vector3& center) {
                CarPart p = MakePart(name, CarMaterial::Needle, role);
                p.pivot = center + n * 0.004f;
                p.axis = n;
                p.mesh.AddBox(Vector3(-0.0025f, -0.008f, -0.001f), Vector3(0.0025f, 0.058f, 0.001f), 1.0f);
                // Align the needle's local +y with the cluster's up direction.
                Vector3 upN = upv;
                upN.Normalize();
                const Matrix basis = Matrix(1, 0, 0, 0,  upN.X, upN.Y, upN.Z, 0,  n.X, n.Y, n.Z, 0,  0, 0, 0, 1);
                p.mesh.Transform(basis);
                p.mesh.ComputeSmoothNormals();
                model.parts.push_back(std::move(p));
            };
            needle("needle_speed", CarPart::Role::NeedleSpeed, c - right * 0.55f);
            needle("needle_rpm", CarPart::Role::NeedleRpm, c + right * 0.55f);
            needle("needle_fuel", CarPart::Role::NeedleFuel, c + right * 0.55f + upv * 0.25f);
            needle("needle_temp", CarPart::Role::NeedleTemp, c - right * 0.55f + upv * 0.25f);
        }
        // Steering column and wheel.
        CarPart steering = MakePart("steering_wheel", CarMaterial::Interior, CarPart::Role::SteeringWheel);
        {
            const float tilt = Sim::Units::DegToRad(vis.steeringWheelTiltDeg);
            const Vector3 n(0.0f, std::sin(tilt), std::cos(tilt));   // rim normal towards the driver
            steering.pivot = vis.steeringWheelCenter;
            steering.axis = n;
            const float R = vis.steeringWheelDiameterM * 0.5f;
            MeshData wheel;
            wheel.AddTorus(Vector3(0, 0, 0), Vector3(0, 0, 1), R, 0.017f, 40, 12);
            // Hub and three spokes (at 90 deg left/right and downwards).
            wheel.AddCylinder(Vector3(0, 0, -0.03f), Vector3(0, 0, 1), 0.065f, 0.045f, 20, true);
            for (const float a : {0.0f, std::numbers::pi_v<float>, std::numbers::pi_v<float> * 1.5f}) {
                MeshData spoke;
                spoke.AddBox(Vector3(0.0f, -0.014f, -0.012f), Vector3(R - 0.01f, 0.014f, 0.012f), 1.0f);
                spoke.Transform(Matrix::CreateRotationZ(a));
                wheel.Append(spoke, Matrix::getIdentityProperty());
            }
            // Local frame: +z is the rim normal; rotate so local z aligns with n.
            const Matrix align = Matrix::CreateRotationX(-tilt);
            wheel.Transform(align);
            wheel.ComputeSmoothNormals();
            steering.mesh = wheel;
            // Column (static).
            MeshData column;
            column.AddCylinder(Vector3(0, 0, 0), Vector3(0, -std::sin(tilt), -std::cos(tilt)), 0.03f, 0.32f, 14, false);
            column.Transform(Matrix::CreateTranslation(vis.steeringWheelCenter - n * 0.03f));
            interior.mesh.Append(column, Matrix::getIdentityProperty());
        }
        // Seats: cushion + backrest, front pair and a rear bench.
        for (const float x : {-0.37f, 0.37f}) {
            AddBoxTo(interior, Vector3(x, floorY + 0.22f, 0.32f), Vector3(0.50f, 0.16f, 0.50f));
            AddOrientedBox(interior, Vector3(x, floorY + 0.62f, 0.62f), Vector3(0.48f, 0.66f, 0.12f), 0.0f, 0.22f);
            AddBoxTo(interior, Vector3(x, floorY + 1.02f, 0.72f), Vector3(0.24f, 0.18f, 0.10f));   // head restraint
        }
        AddBoxTo(interior, Vector3(0.0f, floorY + 0.22f, 1.20f), Vector3(cabinHalf * 2.0f - 0.1f, 0.16f, 0.50f));
        AddOrientedBox(interior, Vector3(0.0f, floorY + 0.60f, 1.50f), Vector3(cabinHalf * 2.0f - 0.1f, 0.62f, 0.12f), 0.0f, 0.20f);
        // Interior rear-view mirror (housing; the face gets the mirror render target).
        CarPart mirror = MakePart("mirror_face", CarMaterial::Chrome, CarPart::Role::Interior);
        {
            const Vector3 c = vis.mirrorCenter;
            AddBoxTo(interior, c + Vector3(0.0f, 0.0f, -0.012f), Vector3(0.24f, 0.07f, 0.02f));
            AddBoxTo(interior, c + Vector3(0.0f, 0.05f, -0.03f), Vector3(0.02f, 0.06f, 0.02f));   // stalk
            const Vector3 right(0.115f, 0, 0);
            const Vector3 upv(0, 0.03f, 0);
            const Vector3 n(0, 0, 1);
            const Vector3 o = c + Vector3(0, 0, 0.0005f);
            mirror.mesh.AddQuad(o - right - upv, o + right - upv, o + right + upv, o - right + upv, n,
                                Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0));
        }

        model.parts.push_back(std::move(paint));
        model.parts.push_back(std::move(glass));
        model.parts.push_back(std::move(trim));
        model.parts.push_back(std::move(shell));
        model.parts.push_back(std::move(doorCards));
        model.parts.push_back(std::move(lampHead));
        model.parts.push_back(std::move(lampTail));
        model.parts.push_back(std::move(lampReverse));
        model.parts.push_back(std::move(indLF));
        model.parts.push_back(std::move(indRF));
        model.parts.push_back(std::move(indLR));
        model.parts.push_back(std::move(indRR));
        model.parts.push_back(std::move(chrome));
        model.parts.push_back(std::move(grille));
        model.parts.push_back(std::move(plates));
        model.parts.push_back(std::move(interior));
        model.parts.push_back(std::move(interiorLight));
        model.parts.push_back(std::move(cluster));
        model.parts.push_back(std::move(steering));
        model.parts.push_back(std::move(mirror));
        for (auto& part : model.parts) {
            if (part.role != CarPart::Role::Interior && part.role != CarPart::Role::Static) {
                continue;
            }
            // Static parts keep their vertices in vehicle space (pivot at the origin).
        }
        return model;
    }
}
