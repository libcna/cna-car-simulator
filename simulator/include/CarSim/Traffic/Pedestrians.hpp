// People on foot in the villages and the town: they walk the pavements, turn at the ends, and
// now and then cross the road at a zebra crossing once nothing is coming. Traffic stops for the
// ones on the road; a car driven at them makes them jump back.
#pragma once

#include "CarSim/Map/MapWorld.hpp"
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <random>
#include <vector>

namespace CarSim::Traffic
{
    struct Pedestrian
    {
        int id = 0;
        int walkway = -1;         // index into the walkways
        float s = 0.0f;           // along the road
        float direction = 1.0f;   // +1 towards +s, -1 back
        float speed = 1.3f;       // m/s walking
        float lateral = 0.0f;     // signed offset from the centreline (right of +s positive)
        // Crossing: from `crossFrom` to `-crossFrom` across the carriageway.
        int crossing = -1;        // index into the crossings, -2 across anywhere, -1 on the pavement
        float crossHalf = 4.0f;   // kerb to centreline of the road being crossed
        float walkedSinceCross = 0.0f;
        float crossFrom = 0.0f;
        float waitAtKerb = 0.0f;  // seconds waited for a gap
        float nextCrossChance = 0.0f;
        float phase = 0.0f;       // walk cycle, radians
        unsigned look = 1;        // clothes and build
        // Derived pose.
        Microsoft::Xna::Framework::Vector3 position{};
        float headingRad = 0.0f;
        bool onRoad = false;      // off the pavement, in the carriageway
    };

    /// A stretch of pavement along one side of a road, clear of the junction mouths.
    struct Walkway
    {
        int road = -1;
        float s0 = 0.0f;
        float s1 = 0.0f;
        float lateral = 0.0f;     // pavement centre, signed
    };

    /// A zebra crossing: where on which road.
    struct Crossing
    {
        int road = -1;
        float s = 0.0f;
        float halfWidth = 4.0f;   // kerb to centreline
    };

    class Pedestrians
    {
    public:
        Pedestrians(const Map::MapWorld& world, std::uint32_t seed);

        /// Keeps about `count` people within reach of `focus` and moves them. `vehicles` are the
        /// traffic cars, `player` the driven car (they dodge it).
        void Update(float dt, const Microsoft::Xna::Framework::Vector3& focus, const std::vector<TrafficVehicle>& vehicles,
                    const PlayerProbe& player, int count = 36);

        [[nodiscard]] const std::vector<Pedestrian>& People() const { return people_; }
        [[nodiscard]] const std::vector<Walkway>& Walkways() const { return walkways_; }
        [[nodiscard]] const std::vector<Crossing>& Crossings() const { return crossings_; }
        /// The people traffic has to stop for (on the road or stepping onto it), as probes.
        [[nodiscard]] std::vector<PlayerProbe> RoadProbes() const;

        /// Places one person on a walkway; `willCross` makes them take the next zebra crossing.
        int Spawn(int walkway, float s, float direction, bool willCross = false);

    private:
        void UpdatePose(Pedestrian& p) const;
        [[nodiscard]] bool GapToCross(const Pedestrian& p, const std::vector<TrafficVehicle>& vehicles, const PlayerProbe& player) const;

        const Map::MapWorld& world_;
        std::mt19937 rng_;
        std::vector<Walkway> walkways_;
        std::vector<Crossing> crossings_;
        std::vector<Pedestrian> people_;
        int nextId_ = 1;
        bool filled_ = false;
    };
}
