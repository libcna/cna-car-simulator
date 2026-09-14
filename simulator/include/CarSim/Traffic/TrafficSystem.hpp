// Ambient traffic: kinematic cars following the lane graph with a car-following model (IDM),
// speed limits, curve speeds, intersection priority (yield/stop/right-hand rule) and reaction to
// the player. Cars spawn around the player and despawn far away. Rendering and collision with
// the player are handled by the callers through the public state.
#pragma once

#include "CarSim/Map/LaneGraph.hpp"
#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Traffic/PlateGenerator.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <random>
#include <string>
#include <vector>

namespace CarSim::Traffic
{
    struct TrafficVehicle
    {
        int id = 0;
        // Path position: on a lane (link < 0) or on a link.
        int lane = -1;
        int link = -1;
        int nextLink = -1;            // chosen link for the end of the current lane
        float s = 0.0f;               // distance along the current element
        float speed = 0.0f;           // m/s
        float acceleration = 0.0f;
        float driverFactor = 1.0f;    // multiplies speed limits (0.9..1.08)
        float lengthM = 4.2f;
        float widthM = 1.75f;
        float heightM = 1.48f;
        float massKg = 1250.0f;
        int paletteIndex = 0;
        Sim::CarStyle::Body body = Sim::CarStyle::Body::Hatchback;   // body variant (dimensions follow the preset)
        unsigned styleSeed = 1;                                       // preset variation
        std::string plate;
        // Derived pose.
        Microsoft::Xna::Framework::Vector3 position{};   // origin on the ground
        Microsoft::Xna::Framework::Vector3 forward{0, 0, -1};
        float headingRad = 0.0f;
        float wheelSpin = 0.0f;       // radians accumulated
        float steerAngle = 0.0f;      // radians at the front wheels
        // State.
        bool waiting = false;         // held at an intersection
        float waitTime = 0.0f;
        bool stoppedAtLine = false;   // stop sign: full stop registered
        bool committed = false;       // released by the deadlock breaker: enters without re-checking
        bool backingOff = false;      // reversing out of a junction stand-off back to the line
        float standoffTime = 0.0f;    // seconds stopped nose to nose inside a junction
        float stunned = 0.0f;         // seconds of forced stop after a collision
        bool brakeLights = false;
        bool indicatorLeft = false;
        bool indicatorRight = false;
        float age = 0.0f;

        [[nodiscard]] Microsoft::Xna::Framework::Matrix WorldMatrix() const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Velocity() const { return forward * speed; }
    };

    struct PlayerProbe
    {
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 forward{0, 0, -1};
        float speed = 0.0f;           // signed forward speed m/s
        float lengthM = 4.05f;
        bool valid = false;
    };

    struct TrafficParams
    {
        float timeHeadway = 1.5f;     // IDM T
        float minGap = 2.5f;          // IDM s0
        float maxAccel = 1.6f;        // IDM a
        float comfortDecel = 2.4f;    // IDM b
        float maxLateralAccel = 2.6f; // curve speed
        float yieldTimeGap = 4.5f;    // seconds: approaching conflicting traffic within this gap blocks entry
        float deadlockSeconds = 14.0f;
        float spawnInterval = 0.8f;
        float spawnViewAngleDeg = 55.0f;
        float spawnViewDistance = 230.0f;
    };

    class TrafficSystem
    {
    public:
        TrafficSystem(const Map::MapWorld& world, std::uint32_t seed = 7);

        void SetDensity(int maxVehicles) { maxVehicles_ = maxVehicles; }
        [[nodiscard]] int MaxVehicles() const { return maxVehicles_; }

        /// Advances all cars; `player` lets the traffic follow, avoid and yield to the player.
        void Update(float dt, const PlayerProbe& player);

        /// Spawns a car on `lane` at `s` (tests and scripted scenes). Returns its id or -1.
        int SpawnOn(int lane, float s, float speed);
        void RemoveAll() { vehicles_.clear(); }

        /// Marks a car as hit: it brakes to a stop and waits before continuing.
        void NotifyCollision(int id, float seconds = 4.0f);

        [[nodiscard]] const std::vector<TrafficVehicle>& Vehicles() const { return vehicles_; }
        [[nodiscard]] std::vector<TrafficVehicle>& Vehicles() { return vehicles_; }
        [[nodiscard]] const PlateGenerator& Plates() const { return plates_; }
        [[nodiscard]] int SpawnedTotal() const { return spawnedTotal_; }

        TrafficParams params;

        /// Picks a body style for a new car: hatchbacks are common, vans rare (public for tests).
        [[nodiscard]] static Sim::CarStyle::Body PickBody(float roll);

        /// Intelligent Driver Model acceleration (public for tests).
        [[nodiscard]] static float IdmAcceleration(float speed, float desiredSpeed, float gap, float leaderSpeed, const TrafficParams& p);

    private:
        struct Leader
        {
            bool found = false;
            float gap = 1e9f;          // bumper to bumper
            float speed = 0.0f;
            int id = -1;               // traffic car id, -1 for the player or a stop line
            bool onConflict = false;   // the leader stands on a crossing connector, not on our path
        };

        void UpdateVehicle(TrafficVehicle& v, float dt, const PlayerProbe& player);
        void UpdatePose(TrafficVehicle& v);
        void ChooseNextLink(TrafficVehicle& v);
        [[nodiscard]] Leader FindLeader(const TrafficVehicle& v, const PlayerProbe& player) const;
        [[nodiscard]] bool MayEnterIntersection(const TrafficVehicle& v, const PlayerProbe& player) const;
        [[nodiscard]] float DesiredSpeedAhead(const TrafficVehicle& v) const;
        void SpawnAroundPlayer(const PlayerProbe& player);
        void Despawn(const PlayerProbe& player);
        [[nodiscard]] float DistanceToEnd(const TrafficVehicle& v) const;
        /// Point `ahead` metres further along the car's path (lane, chosen link, next lane).
        [[nodiscard]] bool PathPointAhead(const TrafficVehicle& v, float ahead, Map::LanePoint& out) const;
        /// Path distance at which `other`'s footprint blocks `v`'s path within `maxAhead`, or -1.
        [[nodiscard]] float PathBlockedBy(const TrafficVehicle& v, const TrafficVehicle& other, float maxAhead) const;
        [[nodiscard]] const TrafficVehicle* FindVehicle(int id) const;
        [[nodiscard]] bool LaneOccupiedNear(int lane, float s, float radius, int ignoreId) const;

        const Map::MapWorld& world_;
        const Map::LaneGraph& lanes_;
        std::vector<TrafficVehicle> vehicles_;
        std::mt19937 rng_;
        PlateGenerator plates_;
        int maxVehicles_ = 20;
        int nextId_ = 1;
        int spawnedTotal_ = 0;
        float spawnTimer_ = 0.0f;
        // Player projection onto the lane graph (refreshed each update).
        int playerLane_ = -1;
        float playerS_ = 0.0f;
    };
}
