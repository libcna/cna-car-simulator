// Authored map description (schema v1) as loaded from content/maps/<name>/*.json.
//
// These structures mirror the JSON files one to one and carry no derived data; the runtime
// structures (RoadNetwork, LaneGraph, TerrainField, MapWorld) are built from them.
// See docs/map-format.md for the schema.
#pragma once

#include "CarSim/Sim/Ground.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace CarSim::Map
{
    inline constexpr int kMapSchemaVersion = 1;

    enum class RoadClass
    {
        ClassI,       // silnice I. třídy
        ClassII,      // silnice II. třídy
        ClassIII,     // silnice III. třídy
        Local,        // místní komunikace (collector street)
        Residential,  // residential street
        Forest,       // forest road (účelová komunikace), paved
        Track         // unpaved track
    };

    enum class CentreLineMarking
    {
        None,
        Solid,
        Dashed,
        SolidForward, // V 3: solid beside the lane travelling towards increasing road s
        SolidReverse  // V 3: solid beside the lane travelling towards decreasing road s
    };

    /// Whether a driver may cross the centre line from this side. This is shared by traffic
    /// planning and the road renderer's authored marking; other overtaking restrictions still
    /// apply even where there is no painted centre line.
    [[nodiscard]] constexpr bool MayCrossCentreLine(const CentreLineMarking marking, const bool forward)
    {
        return marking == CentreLineMarking::None || marking == CentreLineMarking::Dashed ||
               (marking == CentreLineMarking::SolidForward && !forward) ||
               (marking == CentreLineMarking::SolidReverse && forward);
    }

    /// Metres along the authored road curve from its first node. Sections are ordered and
    /// non-overlapping; outside them the road-wide centre line remains in effect.
    struct CentreLineSection
    {
        float fromM = 0.0f;
        float toM = 0.0f;
        CentreLineMarking marking = CentreLineMarking::Dashed;
        bool noOvertaking = false;         // applies in both directions, independent of paint
        bool noOvertakingForward = false;  // B 21a/sight restriction towards increasing road s
        bool noOvertakingReverse = false;  // B 21a/sight restriction towards decreasing road s
    };

    /// Traffic control applied to one road approaching an intersection node.
    enum class ApproachControl
    {
        Priority,      // main road: does not yield (except left turns to oncoming traffic)
        RightHandRule, // uncontrolled: yield to traffic from the right
        Yield,         // P 4 Dej přednost v jízdě!
        Stop,          // P 6 Stůj, dej přednost v jízdě!
        Signal         // světelné signalizační zařízení: the node's signal plan decides
    };

    /// Fixed-time signal plan for one node. The approaches are split into groups that go green
    /// together (typically the main road against the side road); the controller runs the groups
    /// in order, each getting `greenSeconds` of green, `amberSeconds` of amber, and then
    /// `allRedSeconds` with everything red before the next group starts.
    struct SignalPlan
    {
        bool enabled = false;
        float greenSeconds = 20.0f;
        float amberSeconds = 3.0f;
        float allRedSeconds = 2.0f;
        float offsetSeconds = 0.0f;                       // shifts this node's cycle (green waves)
        std::vector<std::vector<std::string>> groups;     // road ids that go green together
    };

    struct RoadNodeSpec
    {
        std::string id;
        Microsoft::Xna::Framework::Vector2 position{};   // map plane (x, z) in metres
        std::optional<float> elevation;                  // overrides the terrain-derived height
        bool urban = false;                              // inside a built-up area (obec): 50 km/h
        std::string name;                                // optional label (e.g. square name)
        std::vector<std::string> mainRoads;              // roads with priority through this node
        std::map<std::string, ApproachControl> approachControl;   // per-road override
        SignalPlan signals;                              // used when an approach is `signal`
        float cornerRadius = 0.0f;                       // 0 = use the road's default smoothing
    };

    struct SidewalkSpec
    {
        float width = 0.0f;        // 0 = no sidewalk
        bool left = false;
        bool right = false;
        float kerbHeight = 0.12f;
    };

    struct RoadSpec
    {
        std::string id;
        std::string name;          // display name (street name)
        std::string number;        // e.g. "II/156" for state roads
        RoadClass roadClass = RoadClass::ClassIII;
        std::vector<std::string> nodes;   // ordered node ids (at least two)
        int lanesPerDirection = 1;
        float laneWidth = 3.0f;
        float edgeStripWidth = 0.25f;     // paved guide strip outside the lane
        float shoulderWidth = 0.5f;       // unpaved shoulder
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
        float speedLimitKmh = 90.0f;      // outside built-up areas
        float urbanSpeedLimitKmh = 50.0f;
        CentreLineMarking centreLine = CentreLineMarking::Dashed;
        bool noOvertaking = false;        // authored road-wide B 21a-style restriction
        std::vector<CentreLineSection> centreLineSections;
        bool edgeLines = true;
        SidewalkSpec sidewalk;
        float cornerRadius = 40.0f;       // smoothing radius applied at interior nodes
        bool oneWay = false;

        [[nodiscard]] CentreLineMarking CentreLineAt(float roadS) const;
        /// Entire passing and return path must be legal, including short restricted sections.
        [[nodiscard]] bool MayOvertakeBetween(float fromRoadS, float toRoadS, bool forward) const;
    };

    enum class TerrainFeatureType
    {
        Hill,    // gaussian bump (height > 0) or basin (height < 0)
        Ridge,   // gaussian profile around a segment from `center` to `end`
        Plateau  // flat top with smooth skirt
    };

    struct TerrainFeatureSpec
    {
        TerrainFeatureType type = TerrainFeatureType::Hill;
        Microsoft::Xna::Framework::Vector2 center{};
        Microsoft::Xna::Framework::Vector2 end{};
        float radius = 100.0f;
        float height = 10.0f;
    };

    enum class RegionType
    {
        Meadow,
        Field,
        Forest,
        Town,
        Square,     // paved town square: the ground is drawn as paving and drives like cobbles
        Yard,       // paved yard or forecourt: concrete slabs, drives like concrete
        Orchard
    };

    struct RegionSpec
    {
        RegionType type = RegionType::Meadow;
        std::vector<Microsoft::Xna::Framework::Vector2> polygon;   // map plane, counter-clockwise or clockwise
        std::string crop;          // fields: "wheat", "rapeseed", "maize", "stubble", "ploughed"
        unsigned seed = 1;
    };

    struct TerrainSpec
    {
        float sizeX = 3000.0f;     // extent in metres, centred on the origin
        float sizeZ = 3000.0f;
        float cellSize = 3.0f;
        float baseHeight = 0.0f;
        float noiseAmplitude = 6.0f;
        float noiseWavelength = 400.0f;
        int noiseOctaves = 4;
        unsigned seed = 7;
        float roadBlendWidth = 14.0f;   // distance over which the terrain blends into the road edge
        std::vector<TerrainFeatureSpec> features;
        std::vector<RegionSpec> regions;
    };

    struct BuildingSpec
    {
        std::string type = "house";   // house, cottage, block, church, barn, shop, hall, chapel
        Microsoft::Xna::Framework::Vector2 position{};
        float rotationDeg = 0.0f;     // heading of the building's front (0 = facing -Z / north)
        float width = 10.0f;          // along the facade
        float depth = 9.0f;
        float eavesHeight = 6.0f;
        float roofPitchDeg = 38.0f;
        int floors = 2;
        std::string wallColor;        // optional "#rrggbb"
        std::string roofColor;        // optional "#rrggbb"
        unsigned seed = 1;
    };

    struct PropSpec
    {
        std::string type;             // bus_stop, bench, lamp, fence, wall, delineator, hydrant, bin, gate, timber_stack
        Microsoft::Xna::Framework::Vector2 position{};
        float rotationDeg = 0.0f;
        float length = 0.0f;          // fences/walls
        float scale = 1.0f;
    };

    /// A car parked in the town: drawn like a traffic car but never moves, and solid.
    struct VehicleSpec
    {
        std::string body = "hatchback";   // hatchback, sedan, estate, suv, van
        Microsoft::Xna::Framework::Vector2 position{};
        float rotationDeg = 0.0f;         // heading of the nose (0 = facing -Z / north)
        unsigned seed = 1;                // picks the style variant, the paint and the plate
    };

    struct SignSpec
    {
        std::string code;             // P1, P2, P3, P4, P6, B20a/b, B21a/b, IZ4a/b, IS3c, IP6, IJ4c, A7a, A12a, A14, A22
        Microsoft::Xna::Framework::Vector2 position{};
        float headingDeg = 0.0f;      // direction the sign face points towards (towards approaching drivers)
        std::string text;             // town names, directions
        float value = 0.0f;           // speed limit for B 20a/b
    };

    struct TreeSpec
    {
        std::string species = "linden";   // linden, oak, birch, spruce, pine, beech, maple
        Microsoft::Xna::Framework::Vector2 position{};
        float scale = 1.0f;
        unsigned seed = 1;
    };

    struct ForestSpec
    {
        std::vector<Microsoft::Xna::Framework::Vector2> polygon;
        float density = 0.03f;            // trees per square metre (0.03 = 300 per hectare)
        std::map<std::string, float> species;   // species weights
        float margin = 6.0f;              // kept free along polygon edges and roads
        unsigned seed = 1;
    };

    struct AvenueSpec
    {
        std::string road;
        std::string species = "linden";
        float spacing = 18.0f;
        float offset = 2.5f;              // from the paved edge
        bool left = true;
        bool right = true;
        std::string fromNode;             // optional sub-range
        std::string toNode;
        unsigned seed = 1;
    };

    struct ObjectsSpec
    {
        std::vector<BuildingSpec> buildings;
        std::vector<PropSpec> props;
        std::vector<SignSpec> signs;
        std::vector<VehicleSpec> vehicles;
        std::vector<TreeSpec> trees;
        std::vector<ForestSpec> forests;
        std::vector<AvenueSpec> avenues;
    };

    struct SpawnSpec
    {
        std::string name;
        Microsoft::Xna::Framework::Vector2 position{};
        float headingDeg = 0.0f;          // 0 = facing -Z (north), 90 = facing +X (east)
    };

    /// A named drive through the map, used by the benchmark scenarios and the validation drive.
    /// The car starts at `spawn` and is driven along the lane graph through the waypoints in
    /// order; nothing is teleported, so a route that cannot be driven fails loudly.
    struct RouteSpec
    {
        std::string name;
        std::string spawn;                                          // player spawn to start from
        std::vector<Microsoft::Xna::Framework::Vector2> waypoints;   // map-plane points, in order
        std::string description;
    };

    struct TrafficSpec
    {
        std::vector<SpawnSpec> playerSpawns;
        std::vector<RouteSpec> routes;
        float densityPerKm = 1.5f;        // AI vehicles per kilometre of lane
        int maxVehicles = 24;
        std::vector<std::string> vehicles;   // vehicle definition ids used by the traffic
        float spawnMinDistance = 60.0f;
        float despawnDistance = 600.0f;
    };

    struct MapInfo
    {
        std::string id;
        std::string displayName;
        std::string description;
        std::string author;
        std::string license;
        int schemaVersion = kMapSchemaVersion;
    };

    struct MapData
    {
        MapInfo info;
        TerrainSpec terrain;
        std::vector<RoadNodeSpec> nodes;
        std::vector<RoadSpec> roads;
        ObjectsSpec objects;
        TrafficSpec traffic;

        [[nodiscard]] const RoadNodeSpec* FindNode(const std::string& id) const;
        [[nodiscard]] const RoadSpec* FindRoad(const std::string& id) const;
    };

    [[nodiscard]] const char* ToString(RoadClass c);
    [[nodiscard]] bool ParseRoadClass(const std::string& text, RoadClass& out);
    [[nodiscard]] bool ParseSurface(const std::string& text, Sim::SurfaceType& out);
    [[nodiscard]] const char* ToString(Sim::SurfaceType s);
    [[nodiscard]] bool ParseRegionType(const std::string& text, RegionType& out);
    [[nodiscard]] bool ParseApproachControl(const std::string& text, ApproachControl& out);
    [[nodiscard]] const char* ToString(ApproachControl c);
}
