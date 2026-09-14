#include "CarSim/Collision/CollisionWorld.hpp"

#include "CarSim/Sim/CarStyle.hpp"

#include "CarSim/Map/MapWorld.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace CarSim::Collision
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    const char* ToString(const ColliderKind k)
    {
        switch (k) {
            case ColliderKind::Building: return "building";
            case ColliderKind::Tree: return "tree";
            case ColliderKind::Post: return "post";
            case ColliderKind::Furniture: return "furniture";
            case ColliderKind::Wall: return "wall";
            case ColliderKind::Boundary: return "boundary";
            case ColliderKind::Vehicle: return "vehicle";
        }
        return "?";
    }

    namespace
    {
        StaticCollider BoxCollider(const ColliderKind kind, const Vector3& centre, const Vector3& half, const float headingRad)
        {
            StaticCollider c;
            c.kind = kind;
            c.isBox = true;
            c.box = Obb::FromHeading(centre, half, headingRad);
            c.centre = centre;
            c.boundingRadius = half.Length();
            return c;
        }

        StaticCollider CylinderCollider(const ColliderKind kind, const Vector3& base, const float radius, const float height)
        {
            StaticCollider c;
            c.kind = kind;
            c.isBox = false;
            c.cylinder.base = base;
            c.cylinder.radius = radius;
            c.cylinder.height = height;
            c.centre = base + Vector3(0.0f, height * 0.5f, 0.0f);
            c.boundingRadius = std::sqrt(radius * radius + height * height * 0.25f);
            return c;
        }

        /// Local offset (x right, y up, z along the facing) rotated into the world.
        Vector3 Offset(const float headingRad, const Vector3& local)
        {
            const Vector3 fwd(std::sin(headingRad), 0.0f, -std::cos(headingRad));
            const Vector3 right(-fwd.Z, 0.0f, fwd.X);
            return right * local.X + Vector3(0.0f, local.Y, 0.0f) + fwd * local.Z;
        }
    }

    void CollisionWorld::Clear()
    {
        statics_.clear();
    }

    void CollisionWorld::AddStatic(const StaticCollider& collider)
    {
        statics_.push_back(collider);
    }

    void CollisionWorld::Finish()
    {
        float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
        for (const auto& c : statics_) {
            minX = std::min(minX, c.centre.X - c.boundingRadius);
            maxX = std::max(maxX, c.centre.X + c.boundingRadius);
            minZ = std::min(minZ, c.centre.Z - c.boundingRadius);
            maxZ = std::max(maxZ, c.centre.Z + c.boundingRadius);
        }
        if (statics_.empty()) {
            minX = minZ = -1.0f;
            maxX = maxZ = 1.0f;
        }
        grid_.Reset(minX - 10.0f, minZ - 10.0f, maxX + 10.0f, maxZ + 10.0f, 25.0f);
        for (std::size_t i = 0; i < statics_.size(); ++i) {
            const auto& c = statics_[i];
            grid_.Insert(static_cast<std::int32_t>(i), c.centre.X - c.boundingRadius, c.centre.Z - c.boundingRadius,
                         c.centre.X + c.boundingRadius, c.centre.Z + c.boundingRadius);
        }
    }

    void CollisionWorld::Build(const Map::MapWorld& world)
    {
        Clear();
        const auto& objects = world.Objects();
        for (const auto& b : objects.Buildings()) {
            const float top = b.height + b.roofHeight * 0.6f;
            const float bottom = -b.foundationDrop;
            const Vector3 centre = b.position + Vector3(0.0f, 0.5f * (top + bottom), 0.0f);
            // The generator's local +z is the facade; FromHeading uses the same convention.
            AddStatic(BoxCollider(ColliderKind::Building, centre, Vector3(b.halfWidth, 0.5f * (top - bottom), b.halfDepth), b.headingRad));
        }
        for (const auto& t : objects.Trees()) {
            AddStatic(CylinderCollider(ColliderKind::Tree, t.position - Vector3(0.0f, 0.5f, 0.0f), t.TrunkRadius(), 6.0f));
        }
        for (const auto& v : objects.Vehicles()) {
            // Parked car: a box the size of its body class, standing on the ground.
            const Sim::CarStyle style = Sim::CarStyle::Preset(v.body, v.seed);
            const Vector3 centre = v.position + Vector3(0.0f, 0.5f * style.height, 0.0f);
            AddStatic(BoxCollider(ColliderKind::Vehicle, centre, Vector3(0.5f * style.width, 0.5f * style.height, 0.5f * style.length), v.headingRad));
        }
        for (const auto& s : objects.Signs()) {
            AddStatic(CylinderCollider(ColliderKind::Post, s.position - Vector3(0.0f, 0.3f, 0.0f), 0.045f, 3.4f));
        }
        for (const auto& p : objects.Props()) {
            using Map::PropType;
            switch (p.type) {
                case PropType::BusStop:
                    AddStatic(BoxCollider(ColliderKind::Furniture, p.position + Vector3(0.0f, 1.3f, 0.0f), Vector3(2.1f, 1.4f, 1.2f), p.headingRad));
                    break;
                case PropType::Memorial:
                    AddStatic(BoxCollider(ColliderKind::Wall, p.position + Vector3(0.0f, 1.3f, 0.0f), Vector3(1.3f, 1.4f, 1.3f), p.headingRad));
                    break;
                case PropType::Bench:
                    AddStatic(BoxCollider(ColliderKind::Furniture, p.position + Vector3(0.0f, 0.5f, 0.0f), Vector3(0.9f, 0.55f, 0.25f), p.headingRad));
                    break;
                case PropType::Lamp:
                    AddStatic(CylinderCollider(ColliderKind::Post, p.position - Vector3(0.0f, 0.3f, 0.0f), 0.10f * p.scale, 7.4f));
                    break;
                case PropType::Fence:
                    AddStatic(BoxCollider(ColliderKind::Wall, p.position + Vector3(0.0f, 0.6f, 0.0f), Vector3(std::max(1.0f, p.length * 0.5f), 0.7f, 0.08f), p.headingRad));
                    break;
                case PropType::Wall:
                    AddStatic(BoxCollider(ColliderKind::Wall, p.position + Vector3(0.0f, 0.85f, 0.0f), Vector3(std::max(1.0f, p.length * 0.5f), 1.05f, 0.3f), p.headingRad));
                    break;
                case PropType::Gate:
                    AddStatic(CylinderCollider(ColliderKind::Post, p.position + Offset(p.headingRad, Vector3(-2.2f, -0.3f, 0.0f)), 0.09f, 1.5f));
                    AddStatic(CylinderCollider(ColliderKind::Post, p.position + Offset(p.headingRad, Vector3(2.2f, -0.3f, 0.0f)), 0.09f, 1.5f));
                    break;
                case PropType::TimberStack:
                    AddStatic(BoxCollider(ColliderKind::Furniture, p.position + Vector3(0.0f, 0.7f, 0.0f), Vector3(std::max(1.5f, p.length * 0.5f), 0.7f, 1.2f), p.headingRad));
                    break;
                case PropType::Hydrant:
                    AddStatic(CylinderCollider(ColliderKind::Post, p.position - Vector3(0.0f, 0.2f, 0.0f), 0.13f, 1.1f));
                    break;
                case PropType::Bin:
                    AddStatic(CylinderCollider(ColliderKind::Post, p.position - Vector3(0.0f, 0.2f, 0.0f), 0.22f, 1.3f));
                    break;
                case PropType::Delineator:   // flexible plastic: no collision
                case PropType::Unknown:
                    break;
            }
        }
        // Boundary walls a little inside the terrain edge.
        const auto& terrain = world.Terrain();
        const float inset = 6.0f;
        const float x0 = terrain.MinX() + inset;
        const float x1 = terrain.MaxX() - inset;
        const float z0 = terrain.MinZ() + inset;
        const float z1 = terrain.MaxZ() - inset;
        const float midY = 0.5f * (terrain.MinHeight() + terrain.MaxHeight());
        const float halfY = 0.5f * (terrain.MaxHeight() - terrain.MinHeight()) + 8.0f;
        const float cx = 0.5f * (x0 + x1);
        const float cz = 0.5f * (z0 + z1);
        const float hx = 0.5f * (x1 - x0);
        const float hz = 0.5f * (z1 - z0);
        AddStatic(BoxCollider(ColliderKind::Boundary, Vector3(cx, midY, z0 - 1.0f), Vector3(hx + 2.0f, halfY, 1.0f), 0.0f));
        AddStatic(BoxCollider(ColliderKind::Boundary, Vector3(cx, midY, z1 + 1.0f), Vector3(hx + 2.0f, halfY, 1.0f), 0.0f));
        AddStatic(BoxCollider(ColliderKind::Boundary, Vector3(x0 - 1.0f, midY, cz), Vector3(1.0f, halfY, hz + 2.0f), 0.0f));
        AddStatic(BoxCollider(ColliderKind::Boundary, Vector3(x1 + 1.0f, midY, cz), Vector3(1.0f, halfY, hz + 2.0f), 0.0f));
        Finish();
    }

    Obb CollisionWorld::VehicleBox(const Sim::Vehicle& vehicle)
    {
        const auto& def = vehicle.Definition();
        const float length = def.chassis.lengthM;
        const float width = def.chassis.widthM;
        const float height = def.chassis.heightM;
        const float overhang = std::max(0.0f, length - def.WheelbaseM());
        // The body generator puts 55 % of the overhang at the front, so the box centre sits a
        // little ahead of the wheelbase centre (negative z is forward).
        const Vector3 localCentre(0.0f, height * 0.5f, -0.05f * overhang);
        const Sim::RigidBody& body = vehicle.Body();
        const Vector3 origin = vehicle.OriginPosition();
        const Vector3 centre = origin + body.ToWorldDirection(localCentre);
        return Obb::FromRotation(centre, body.Rotation(), Vector3(width * 0.5f, height * 0.5f, length * 0.5f));
    }

    void CollisionWorld::ApplyContact(Sim::Vehicle& vehicle, const Contact& contact, const float otherInverseMass, const Vector3& otherVelocity,
                                      const ColliderKind kind, std::vector<ContactEvent>& events, Vector3* appliedImpulse) const
    {
        Sim::RigidBody& body = vehicle.Body();
        const Vector3& n = contact.normal;
        const Vector3 p = contact.point;
        const Vector3 r = p - body.Position();
        const Matrix invI = body.InverseInertiaWorld();
        const Vector3 relative = body.VelocityAtWorldPoint(p) - otherVelocity;
        const float vn = Vector3::Dot(relative, n);
        Vector3 applied(0.0f, 0.0f, 0.0f);
        float normalImpulse = 0.0f;
        if (vn < 0.0f) {
            const Vector3 rn = Vector3::Cross(r, n);
            const float k = body.InverseMass() + otherInverseMass + Vector3::Dot(n, Vector3::Cross(Vector3::TransformNormal(rn, invI), r));
            normalImpulse = -(1.0f + params.restitution) * vn / std::max(1e-6f, k);
            body.ApplyImpulse(n * normalImpulse, p);
            applied = applied + n * normalImpulse;
            // Coulomb friction along the tangential relative velocity.
            const Vector3 after = body.VelocityAtWorldPoint(p) - otherVelocity;
            Vector3 vt = after - n * Vector3::Dot(after, n);
            const float speed = vt.Length();
            if (speed > 1e-4f) {
                const Vector3 t = vt * (1.0f / speed);
                const Vector3 rt = Vector3::Cross(r, t);
                const float kt = body.InverseMass() + otherInverseMass + Vector3::Dot(t, Vector3::Cross(Vector3::TransformNormal(rt, invI), r));
                float jt = -speed / std::max(1e-6f, kt);
                const float limit = params.friction * normalImpulse;
                jt = std::clamp(jt, -limit, limit);
                body.ApplyImpulse(t * jt, p);
                applied = applied + t * jt;
            }
        }
        // Positional correction keeps resting contacts from sinking in.
        const float excess = contact.penetration - params.slop;
        if (excess > 0.0f) {
            const float total = body.InverseMass() + otherInverseMass;
            const float share = total > 0.0f ? body.InverseMass() / total : 1.0f;
            const float move = std::min(params.maxCorrection, excess * params.correction) * share;
            body.SetPosition(body.Position() + n * move);
        }
        if (vn < -0.05f || normalImpulse > 0.0f) {
            ContactEvent e;
            e.point = p;
            e.normal = n;
            e.normalImpulse = normalImpulse;
            e.closingSpeed = -vn;
            e.kind = kind;
            events.push_back(e);
        }
        if (appliedImpulse) {
            *appliedImpulse = *appliedImpulse + applied;
        }
    }

    void CollisionWorld::ResolveVehicle(Sim::Vehicle& vehicle, std::vector<ContactEvent>& events) const
    {
        if (statics_.empty()) {
            return;
        }
        // Two passes handle corners where the first correction pushes into a neighbour.
        for (int pass = 0; pass < 2; ++pass) {
            const Obb box = VehicleBox(vehicle);
            const float reach = box.BoundingRadius() + 0.5f;
            std::vector<std::int32_t> ids;
            grid_.QueryUnique(box.centre.X - reach, box.centre.Z - reach, box.centre.X + reach, box.centre.Z + reach, ids);
            bool any = false;
            for (const std::int32_t id : ids) {
                const StaticCollider& c = statics_[static_cast<std::size_t>(id)];
                if (Vector3::DistanceSquared(c.centre, box.centre) > (c.boundingRadius + reach) * (c.boundingRadius + reach)) {
                    continue;
                }
                Contact contact;
                const Obb current = VehicleBox(vehicle);
                const bool hit = c.isBox ? IntersectObbObb(current, c.box, contact) : IntersectObbCylinder(current, c.cylinder, contact);
                if (!hit) {
                    continue;
                }
                any = true;
                ApplyContact(vehicle, contact, 0.0f, Vector3(0.0f, 0.0f, 0.0f), c.kind, events, nullptr);
            }
            if (!any) {
                break;
            }
        }
    }

    void CollisionWorld::ResolveVehiclePair(Sim::Vehicle& a, Sim::Vehicle& b, std::vector<ContactEvent>& events) const
    {
        const Obb boxA = VehicleBox(a);
        const Obb boxB = VehicleBox(b);
        Contact contact;
        if (!IntersectObbObb(boxA, boxB, contact)) {
            return;
        }
        Sim::RigidBody& bodyA = a.Body();
        Sim::RigidBody& bodyB = b.Body();
        const Vector3& n = contact.normal;   // from B towards A
        const Vector3 p = contact.point;
        const Vector3 ra = p - bodyA.Position();
        const Vector3 rb = p - bodyB.Position();
        const Matrix invIa = bodyA.InverseInertiaWorld();
        const Matrix invIb = bodyB.InverseInertiaWorld();
        const Vector3 relative = bodyA.VelocityAtWorldPoint(p) - bodyB.VelocityAtWorldPoint(p);
        const float vn = Vector3::Dot(relative, n);
        float j = 0.0f;
        if (vn < 0.0f) {
            const Vector3 ran = Vector3::Cross(ra, n);
            const Vector3 rbn = Vector3::Cross(rb, n);
            const float k = bodyA.InverseMass() + bodyB.InverseMass() + Vector3::Dot(n, Vector3::Cross(Vector3::TransformNormal(ran, invIa), ra)) +
                            Vector3::Dot(n, Vector3::Cross(Vector3::TransformNormal(rbn, invIb), rb));
            j = -(1.0f + params.restitution) * vn / std::max(1e-6f, k);
            bodyA.ApplyImpulse(n * j, p);
            bodyB.ApplyImpulse(n * -j, p);
            const Vector3 after = bodyA.VelocityAtWorldPoint(p) - bodyB.VelocityAtWorldPoint(p);
            Vector3 vt = after - n * Vector3::Dot(after, n);
            const float speed = vt.Length();
            if (speed > 1e-4f) {
                const Vector3 t = vt * (1.0f / speed);
                const Vector3 rat = Vector3::Cross(ra, t);
                const Vector3 rbt = Vector3::Cross(rb, t);
                const float kt = bodyA.InverseMass() + bodyB.InverseMass() + Vector3::Dot(t, Vector3::Cross(Vector3::TransformNormal(rat, invIa), ra)) +
                                 Vector3::Dot(t, Vector3::Cross(Vector3::TransformNormal(rbt, invIb), rb));
                float jt = -speed / std::max(1e-6f, kt);
                const float limit = params.friction * j;
                jt = std::clamp(jt, -limit, limit);
                bodyA.ApplyImpulse(t * jt, p);
                bodyB.ApplyImpulse(t * -jt, p);
            }
        }
        const float excess = contact.penetration - params.slop;
        if (excess > 0.0f) {
            const float total = bodyA.InverseMass() + bodyB.InverseMass();
            const float move = std::min(params.maxCorrection, excess * params.correction);
            bodyA.SetPosition(bodyA.Position() + n * (move * bodyA.InverseMass() / total));
            bodyB.SetPosition(bodyB.Position() - n * (move * bodyB.InverseMass() / total));
        }
        if (vn < -0.05f) {
            ContactEvent e;
            e.point = p;
            e.normal = n;
            e.normalImpulse = j;
            e.closingSpeed = -vn;
            e.kind = ColliderKind::Vehicle;
            events.push_back(e);
        }
    }

    Vector3 CollisionWorld::ResolveVehicleAgainstBox(Sim::Vehicle& vehicle, const Obb& other, const float otherMass, const Vector3& otherVelocity,
                                                     std::vector<ContactEvent>& events) const
    {
        Contact contact;
        if (!IntersectObbObb(VehicleBox(vehicle), other, contact)) {
            return Vector3(0.0f, 0.0f, 0.0f);
        }
        Vector3 applied(0.0f, 0.0f, 0.0f);
        ApplyContact(vehicle, contact, otherMass > 0.0f ? 1.0f / otherMass : 0.0f, otherVelocity, ColliderKind::Vehicle, events, &applied);
        return applied;
    }

    bool CollisionWorld::Overlaps(const Obb& box) const
    {
        const float reach = box.BoundingRadius();
        std::vector<std::int32_t> ids;
        grid_.QueryUnique(box.centre.X - reach, box.centre.Z - reach, box.centre.X + reach, box.centre.Z + reach, ids);
        for (const std::int32_t id : ids) {
            const StaticCollider& c = statics_[static_cast<std::size_t>(id)];
            Contact contact;
            if (c.isBox ? IntersectObbObb(box, c.box, contact) : IntersectObbCylinder(box, c.cylinder, contact)) {
                return true;
            }
        }
        return false;
    }
}
