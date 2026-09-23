// Buses and lorries for the traffic: box-built bodies on large wheels. The passenger-car
// generator lofts a skin through a car's profile, which does not stretch into a twelve-metre
// bus; these are assembled from boxes instead, with the same parts, materials and wheel roles,
// so VehicleRenderer and the traffic renderer treat them like any other model.
#include "CarBody.hpp"

namespace CarSim::Render
{
    using namespace CarBody;
    using Sim::CarStyle;

    namespace
    {
        /// Box between two corners, any order.
        void Block(CarPart& part, const Vector3& a, const Vector3& b)
        {
            part.mesh.AddBox(Vector3::Min(a, b), Vector3::Max(a, b), 1.0f);
        }

        /// Adds the lamp lens box to `part` and a glow anchor facing along `normal`.
        void Lamp(CarModel& model, CarPart& part, const Vector3& a, const Vector3& b, const Vector3& normal, const CarMaterial kind,
                  const bool left)
        {
            Block(part, a, b);
            LampGlow glow;
            glow.position = (a + b) * 0.5f + normal * 0.02f;
            glow.normal = normal;
            glow.kind = kind;
            glow.left = left;
            model.lamps.push_back(glow);
        }

        /// Solid body skin between z0 and z1 with wheel wells cut out at the given axles: the
        /// lower band is split around each arch, the upper band runs through.
        void BodyWithWells(CarPart& paint, CarPart& trim, const float halfWidth, const float z0, const float z1, const float yBottom,
                           const float yWell, const float yTop, const std::vector<float>& axles, const float wellHalf, const float innerHalf)
        {
            Block(paint, Vector3(-halfWidth, yWell, z0), Vector3(halfWidth, yTop, z1));
            float z = z0;
            for (const float axle : axles) {
                const float a = axle - wellHalf;
                const float b = axle + wellHalf;
                if (b < z0 || a > z1) continue;
                if (a > z) Block(paint, Vector3(-halfWidth, yBottom, z), Vector3(halfWidth, yWell, a));
                // The well itself: a dark box inboard of the tyres closes it.
                Block(trim, Vector3(-innerHalf, yBottom + 0.05f, std::max(a, z0)), Vector3(innerHalf, yWell, std::min(b, z1)));
                z = b;
            }
            if (z < z1) Block(paint, Vector3(-halfWidth, yBottom, z), Vector3(halfWidth, yWell, z1));
        }
    }

    CarModel GenerateHeavyVehicle(const CarStyle& style)
    {
        CarModel model;
        model.style = style;
        model.wheelRadius = style.wheelRadius;
        const bool bus = style.body == CarStyle::Body::Bus;
        const float W = style.width * 0.5f;
        const float zF = style.FrontZ();
        const float zR = style.RearZ();
        const float zFA = style.FrontAxleZ();
        const float zRA = style.RearAxleZ();
        const float H = style.height;
        const float yb = style.rideHeight;
        const float well = style.wheelRadius + 0.10f;
        const float wellTop = style.wheelRadius * 2.0f + 0.08f;
        const float inner = style.track * 0.5f - style.tyreWidth * 0.5f - 0.06f;

        CarPart paint = MakePart("body_paint", CarMaterial::Paint);
        CarPart glass = MakePart("body_glass", CarMaterial::Glass);
        CarPart trim = MakePart("body_trim", CarMaterial::BlackTrim);
        CarPart gloss = MakePart("body_gloss", CarMaterial::GlossBlack);
        CarPart chrome = MakePart("chrome", CarMaterial::Chrome);
        CarPart plates = MakePart("plates", CarMaterial::Plate);
        CarPart lampHead = MakePart("lamp_head", CarMaterial::LampHead);
        CarPart lampTail = MakePart("lamp_tail", CarMaterial::LampTail);
        CarPart lampReverse = MakePart("lamp_reverse", CarMaterial::LampReverse);
        CarPart indLF = MakePart("indicator_left_front", CarMaterial::LampIndicator);
        CarPart indRF = MakePart("indicator_right_front", CarMaterial::LampIndicator);
        CarPart indLR = MakePart("indicator_left_rear", CarMaterial::LampIndicator);
        CarPart indRR = MakePart("indicator_right_rear", CarMaterial::LampIndicator);
        const Vector3 ahead(0.0f, 0.0f, -1.0f);
        const Vector3 behind(0.0f, 0.0f, 1.0f);
        constexpr float kGlassProud = 0.012f;

        float cabRear = zR;          // lorry: where the cab ends
        if (bus) {
            // One long body, glazing band down both sides, a big windscreen and a small rear window.
            BodyWithWells(paint, trim, W, zF, zR, yb, wellTop, H, {zFA, zRA}, well, inner);
            const float g0 = 1.25f, g1 = H - 0.28f;
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * (W + kGlassProud * 0.5f);
                Block(glass, Vector3(x - kGlassProud * 0.5f, g0, zF + 0.35f), Vector3(x + kGlassProud * 0.5f, g1, zR - 0.45f));
                // Window pillars every 1.45 m.
                for (float z = zF + 0.35f; z <= zR - 0.44f; z += 1.45f) {
                    Block(gloss, Vector3(x - 0.016f, g0 - 0.02f, z - 0.06f), Vector3(x + 0.016f, g1 + 0.02f, z + 0.06f));
                }
            }
            // Doors on the kerb side (right): tall glazed leaves, the front one behind the axle.
            for (const float zd : {zF + 0.30f, 0.0f}) {
                Block(gloss, Vector3(W, yb + 0.05f, zd), Vector3(W + 0.02f, g1, zd + 1.20f));
                Block(glass, Vector3(W + 0.02f, yb + 0.55f, zd + 0.08f), Vector3(W + 0.032f, g1 - 0.08f, zd + 1.12f));
                Block(trim, Vector3(W + 0.02f, yb + 0.05f, zd + 0.58f), Vector3(W + 0.035f, g1, zd + 0.62f));
            }
            // Windscreen, destination display, rear window.
            Block(glass, Vector3(-W + 0.06f, 0.95f, zF - kGlassProud), Vector3(W - 0.06f, H - 0.40f, zF));
            Block(gloss, Vector3(-W + 0.18f, H - 0.38f, zF - 0.02f), Vector3(W - 0.18f, H - 0.08f, zF));
            Block(lampReverse, Vector3(-W + 0.40f, H - 0.32f, zF - 0.025f), Vector3(W - 0.40f, H - 0.14f, zF - 0.02f));   // lit display text band
            Block(glass, Vector3(-W + 0.30f, 1.75f, zR), Vector3(W - 0.30f, H - 0.35f, zR + kGlassProud));
            // Roof air-conditioning pod and bumpers.
            Block(trim, Vector3(-W + 0.35f, H, -1.8f), Vector3(W - 0.35f, H + 0.24f, 1.4f));
            Block(trim, Vector3(-W - 0.02f, yb, zF - 0.06f), Vector3(W + 0.02f, yb + 0.42f, zF + 0.05f));
            Block(trim, Vector3(-W - 0.02f, yb, zR - 0.05f), Vector3(W + 0.02f, yb + 0.42f, zR + 0.06f));
        } else {
            // Lorry: cab over the front axle, chassis rails, box body behind.
            cabRear = zF + 2.10f;
            const float cabTop = std::min(H - 0.35f, 2.95f);
            BodyWithWells(paint, trim, W - 0.03f, zF, cabRear, yb + 0.15f, wellTop, cabTop, {zFA}, well, inner);
            Block(glass, Vector3(-W + 0.10f, 1.55f, zF - kGlassProud), Vector3(W - 0.10f, cabTop - 0.30f, zF));
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * (W - 0.03f + kGlassProud * 0.5f);
                Block(glass, Vector3(x - kGlassProud * 0.5f, 1.65f, zF + 0.25f), Vector3(x + kGlassProud * 0.5f, cabTop - 0.35f, zF + 1.25f));
                // Big lorry mirrors on arms.
                Block(trim, Vector3(side * (W - 0.03f), 2.15f, zF + 0.20f), Vector3(side * (W + 0.25f), 2.20f, zF + 0.26f));
                Block(trim, Vector3(side * (W + 0.20f), 1.75f, zF + 0.18f), Vector3(side * (W + 0.30f), 2.30f, zF + 0.30f));
            }
            Block(trim, Vector3(-W + 0.25f, 0.55f, zF - 0.03f), Vector3(W - 0.25f, 1.05f, zF + 0.02f));   // grille
            Block(trim, Vector3(-W, yb, zF - 0.08f), Vector3(W, yb + 0.30f, zF + 0.10f));                  // bumper
            // Chassis rails and the box body.
            Block(trim, Vector3(-0.50f, 0.62f, cabRear), Vector3(0.50f, 0.92f, zR - 0.10f));
            const float boxBottom = wellTop + 0.05f;
            Block(paint, Vector3(-W, boxBottom, cabRear + 0.12f), Vector3(W, H, zR));
            // Rear doors: two leaves with a dark gap and hinges; underrun guard.
            Block(gloss, Vector3(-0.015f, boxBottom + 0.05f, zR), Vector3(0.015f, H - 0.05f, zR + 0.012f));
            for (const float x : {-W + 0.06f, W - 0.06f}) {
                for (const float y : {boxBottom + 0.35f, (boxBottom + H) * 0.5f, H - 0.35f}) {
                    Block(chrome, Vector3(x - 0.03f, y - 0.05f, zR), Vector3(x + 0.03f, y + 0.05f, zR + 0.02f));
                }
            }
            Block(trim, Vector3(-W + 0.10f, 0.45f, zR - 0.05f), Vector3(W - 0.10f, 0.60f, zR + 0.02f));
            // Side guards between the axles.
            for (const float side : {-1.0f, 1.0f}) {
                Block(trim, Vector3(side * (W - 0.08f), 0.55f, zFA + well + 0.1f), Vector3(side * (W - 0.05f), 0.70f, zRA - well - 0.1f));
            }
        }

        // Lamps: headlamps and indicators on the front, tail, reverse and indicators on the back.
        const float headY = bus ? 0.72f : 0.80f;
        for (const float side : {-1.0f, 1.0f}) {
            const bool left = side < 0.0f;
            const float x0 = side * (W - 0.12f), x1 = side * (W - 0.45f);
            Lamp(model, lampHead, Vector3(x0, headY, zF - 0.03f), Vector3(x1, headY + 0.16f, zF - 0.012f), ahead, CarMaterial::LampHead, left);
            Lamp(model, left ? indLF : indRF, Vector3(x0, headY + 0.20f, zF - 0.03f), Vector3(side * (W - 0.26f), headY + 0.28f, zF - 0.012f),
                 ahead, CarMaterial::LampIndicator, left);
            const float tailY = bus ? 0.85f : 0.62f;
            Lamp(model, lampTail, Vector3(x0, tailY, zR + 0.012f), Vector3(side * (W - 0.30f), tailY + 0.22f, zR + 0.03f), behind,
                 CarMaterial::LampTail, left);
            Lamp(model, left ? indLR : indRR, Vector3(x0, tailY + 0.26f, zR + 0.012f), Vector3(side * (W - 0.30f), tailY + 0.36f, zR + 0.03f),
                 behind, CarMaterial::LampIndicator, left);
            Lamp(model, lampReverse, Vector3(side * (W - 0.34f), tailY, zR + 0.012f), Vector3(side * (W - 0.44f), tailY + 0.10f, zR + 0.03f),
                 behind, CarMaterial::LampReverse, left);
        }

        // Plates.
        {
            const float plateW = 0.52f, plateH = 0.11f;
            model.frontPlateCenter = Vector3(0.0f, bus ? 0.48f : 0.60f, zF - (bus ? 0.065f : 0.085f));
            model.rearPlateCenter = Vector3(0.0f, bus ? 0.62f : 0.80f, zR + 0.035f);
            const Vector3 f = model.frontPlateCenter;
            plates.mesh.AddQuad(f + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0), f + Vector3(-plateW * 0.5f, plateH * 0.5f, 0),
                                f + Vector3(plateW * 0.5f, plateH * 0.5f, 0), f + Vector3(plateW * 0.5f, -plateH * 0.5f, 0),
                                Vector3(0, 0, -1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
            const Vector3 b = model.rearPlateCenter;
            plates.mesh.AddQuad(b + Vector3(plateW * 0.5f, -plateH * 0.5f, 0), b + Vector3(plateW * 0.5f, plateH * 0.5f, 0),
                                b + Vector3(-plateW * 0.5f, plateH * 0.5f, 0), b + Vector3(-plateW * 0.5f, -plateH * 0.5f, 0),
                                Vector3(0, 0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1));
        }

        // Wheels: the lorry's rear axle carries twin tyres (drawn as one wide tyre).
        const CarPart::Role roles[4] = {CarPart::Role::WheelFL, CarPart::Role::WheelFR, CarPart::Role::WheelRL, CarPart::Role::WheelRR};
        const char* names[4] = {"FL", "FR", "RL", "RR"};
        for (int i = 0; i < 4; ++i) {
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            const bool rear = i >= 2;
            const float halfWidth = rear && !bus ? style.tyreWidth * 0.95f : style.tyreWidth * 0.5f;
            const float x = side * (style.track * 0.5f + (rear && !bus ? style.tyreWidth * 0.3f : 0.0f));
            const Vector3 centre(x, style.wheelRadius, rear ? zRA : zFA);
            model.wheelCenters[static_cast<std::size_t>(i)] = centre;
            MeshData tyreMesh, rimMesh, discMesh;
            BuildWheel(style, halfWidth, tyreMesh, rimMesh, discMesh);
            if (side < 0.0f) {
                for (MeshData* m : {&tyreMesh, &rimMesh, &discMesh}) {
                    m->Transform(Matrix::CreateScale(-1.0f, 1.0f, 1.0f));
                    m->FlipWinding();
                    for (auto& v : m->vertices) v.normal = -v.normal;
                }
            }
            CarPart tyre = MakePart(std::string("tyre_") + names[i], CarMaterial::Tyre, roles[i]);
            CarPart rim = MakePart(std::string("rim_") + names[i], CarMaterial::Rim, roles[i]);
            tyre.pivot = rim.pivot = centre;
            tyre.axis = rim.axis = Vector3(1, 0, 0);
            tyre.mesh = std::move(tyreMesh);
            rim.mesh = std::move(rimMesh);
            model.parts.push_back(std::move(tyre));
            model.parts.push_back(std::move(rim));
        }
        (void)cabRear;

        model.bodyTriangles = static_cast<int>(paint.mesh.TriangleCount() + glass.mesh.TriangleCount() + trim.mesh.TriangleCount() +
                                               gloss.mesh.TriangleCount());
        for (CarPart* p : {&paint, &glass, &trim, &gloss, &chrome, &plates, &lampHead, &lampTail, &lampReverse, &indLF, &indRF, &indLR, &indRR}) {
            if (p->mesh.TriangleCount() > 0) model.parts.push_back(std::move(*p));
        }
        return model;
    }
}
