// Collision world: static colliders built from the map's placed objects plus map boundary
// walls, and impulse-based response for the physics vehicles (player) against statics,
// other physics vehicles and moving boxes (traffic). Contacts are reported as events for
// audio and debugging.
#pragma once

#include "CarSim/Collision/Shapes.hpp"
#include "CarSim/Map/SpatialGrid.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include <cstddef>
#include <vector>

namespace CarSim::Map
{
    class MapWorld;
}

namespace CarSim::Collision
{
    enum class ColliderKind
    {
        Building,
        Tree,
        Post,
        Furniture,
        Wall,
        Boundary,
        Vehicle
    };

    [[nodiscard]] const char* ToString(ColliderKind k);

    struct StaticCollider
    {
        ColliderKind kind = ColliderKind::Building;
        bool isBox = true;
        Obb box;
        VerticalCylinder cylinder;
        Microsoft::Xna::Framework::Vector3 centre{};
        float boundingRadius = 1.0f;
    };

    struct ContactEvent
    {
        Microsoft::Xna::Framework::Vector3 point{};
        Microsoft::Xna::Framework::Vector3 normal{};
        float normalImpulse = 0.0f;     // N s applied to the vehicle
        float closingSpeed = 0.0f;      // m/s along the normal before the impulse
        ColliderKind kind = ColliderKind::Building;
    };

    struct ResponseParams
    {
        float restitution = 0.12f;
        float friction = 0.55f;
        float slop = 0.01f;             // penetration tolerated without correction
        float correction = 0.65f;       // fraction of the remaining penetration removed per resolve
        float maxCorrection = 0.25f;    // metres per resolve
    };

    class CollisionWorld
    {
    public:
        /// Builds static colliders from the map (buildings, trees, posts, furniture, boundary walls).
        void Build(const Map::MapWorld& world);
        void Clear();
        void AddStatic(const StaticCollider& collider);
        void Finish();   // rebuilds the grid after AddStatic calls outside Build

        /// Oriented chassis box of a vehicle in world space (from its definition and body pose).
        [[nodiscard]] static Obb VehicleBox(const Sim::Vehicle& vehicle);

        /// Resolves the vehicle against every static collider it touches. Appends events.
        void ResolveVehicle(Sim::Vehicle& vehicle, std::vector<ContactEvent>& events) const;

        /// Sweeps the helicopter hull, tail, skids and rotor from the previous origin to the current
        /// one, stopping at static geometry. This also prevents fast flight from crossing walls.
        void ResolveFlight(Sim::Vehicle& vehicle, const Microsoft::Xna::Framework::Vector3& previousOrigin,
                           std::vector<ContactEvent>& events) const;

        /// Sweeps the helicopter's full hull against one moving traffic car. Returns true on
        /// contact so its traffic controller can stop the struck car as well.
        [[nodiscard]] bool ResolveFlightAgainstBox(Sim::Vehicle& vehicle,
                                                   const Microsoft::Xna::Framework::Vector3& previousOrigin,
                                                   const Obb& other,
                                                   const Microsoft::Xna::Framework::Vector3& otherVelocity,
                                                   std::vector<ContactEvent>& events) const;

        /// Resolves two physics vehicles against each other.
        void ResolveVehiclePair(Sim::Vehicle& a, Sim::Vehicle& b, std::vector<ContactEvent>& events) const;

        /// Resolves a vehicle against a moving box of mass `otherMass` (e.g. a traffic car); only the
        /// vehicle receives the impulse. Returns the applied impulse (zero when no contact).
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 ResolveVehicleAgainstBox(Sim::Vehicle& vehicle, const Obb& other, float otherMass,
                                                                                const Microsoft::Xna::Framework::Vector3& otherVelocity,
                                                                                std::vector<ContactEvent>& events) const;

        /// True when a box overlaps any static collider (spawn checks).
        [[nodiscard]] bool Overlaps(const Obb& box) const;

        [[nodiscard]] std::size_t StaticCount() const { return statics_.size(); }
        [[nodiscard]] const std::vector<StaticCollider>& Statics() const { return statics_; }

        ResponseParams params;

    private:
        void ApplyContact(Sim::Vehicle& vehicle, const Contact& contact, float otherInverseMass, const Microsoft::Xna::Framework::Vector3& otherVelocity,
                          ColliderKind kind, std::vector<ContactEvent>& events, Microsoft::Xna::Framework::Vector3* appliedImpulse) const;

        std::vector<StaticCollider> statics_;
        Map::SpatialGrid grid_;
    };
}
