// Resolves the authored objects of a map (buildings, trees, forests, avenues, signs, props)
// into world-space placements on the terrain, plus automatically generated roadside
// delineators. Pure data: the world renderer builds meshes from it and the collision system
// builds colliders from it.
#pragma once

#include "CarSim/Map/MapData.hpp"
#include "CarSim/Sim/CarStyle.hpp"
#include "CarSim/Map/SpatialGrid.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>
#include <vector>

namespace CarSim::Map
{
    class MapWorld;

    enum class TreeSpecies
    {
        Linden,
        Oak,
        Birch,
        Maple,
        Beech,
        Spruce,
        Pine,
        Bush        // roadside and forest-edge shrub (no trunk)
    };

    [[nodiscard]] bool ParseTreeSpecies(const std::string& text, TreeSpecies& out);
    [[nodiscard]] const char* ToString(TreeSpecies s);
    [[nodiscard]] bool IsConifer(TreeSpecies s);

    struct PlacedBuilding
    {
        const BuildingSpec* spec = nullptr;
        Microsoft::Xna::Framework::Vector3 position{};   // footprint centre at the foundation height
        float headingRad = 0.0f;                        // facade direction (atan2(x, -z))
        float foundationDrop = 0.0f;                    // extra wall height below `position` to reach the lowest corner
        float halfWidth = 5.0f;                         // along the facade
        float halfDepth = 4.5f;
        float height = 6.0f;                            // eaves height above position
        float roofHeight = 3.0f;                        // ridge above the eaves

        /// Half the width of the drive-through passage in the middle of the facade (the arch of a
        /// castle gatehouse, which is built over its road), or 0 for a solid building.
        float PassageHalfWidth() const;
    };

    /// A parked car: a static body drawn like a traffic car and solid in the collision world.
    struct PlacedVehicle
    {
        Sim::CarStyle::Body body = Sim::CarStyle::Body::Hatchback;
        Microsoft::Xna::Framework::Vector3 position{};   // origin on the ground
        float headingRad = 0.0f;                         // nose direction (atan2(x, -z))
        unsigned seed = 1;                               // style variant, paint and plate
    };

    struct PlacedTree
    {
        TreeSpecies species = TreeSpecies::Linden;
        Microsoft::Xna::Framework::Vector3 position{};
        float scale = 1.0f;
        float rotationRad = 0.0f;
        unsigned seed = 0;
        bool collidable = true;   // low visual undergrowth can be walked through
        [[nodiscard]] float TrunkRadius() const;
        [[nodiscard]] float Height() const;
        [[nodiscard]] float CrownRadius() const;
    };

    struct PlacedSign
    {
        const SignSpec* spec = nullptr;
        Microsoft::Xna::Framework::Vector3 position{};   // post base on the ground
        float headingRad = 0.0f;                        // direction the face points to
        bool urban = false;                             // mounted at sidewalk height inside built-up areas
    };

    enum class PropType
    {
        BusStop,
        Bench,
        Lamp,
        Fence,
        Wall,
        Gate,
        TimberStack,
        Hydrant,
        Bin,
        Planter,        // low stone flower bed on the paved town square
        Delineator,     // Z 11a/b roadside post (generated)
        WireFence,      // wire mesh fence on steel posts (generated around plots)
        Hedge,          // clipped hedge (generated around plots)
        Shed,           // garden shed behind a house (generated)
        UtilityPole,    // wooden pole with a crossarm along village roads (generated)
        Memorial,       // stone column on a stepped plinth with a cross (town squares)
        FuelCanopy,     // filling station canopy on four columns
        FuelPump,       // filling station pump with a hose and a display
        SignalHead,     // traffic signal mast and housing (generated at signalised junctions)
        Unknown
    };

    [[nodiscard]] bool ParsePropType(const std::string& text, PropType& out);

    struct PlacedProp
    {
        PropType type = PropType::Unknown;
        Microsoft::Xna::Framework::Vector3 position{};
        float headingRad = 0.0f;
        float length = 0.0f;      // fences, walls, timber stacks
        float scale = 1.0f;
        bool reflectorRight = true;   // delineators: orange reflector faces the driver on the right
    };

    /// One traffic signal head: a mast on the kerb of a signalised approach, its lenses facing
    /// the traffic that is coming towards the junction.
    struct PlacedSignal
    {
        Microsoft::Xna::Framework::Vector3 position{};   // foot of the mast
        float headingRad = 0.0f;                         // direction the lenses face
        int intersection = -1;
        int group = -1;                                  // signal group of the approach
    };

    class ObjectPlacement
    {
    public:
        void Build(const MapWorld& world, std::vector<std::string>& warnings);

        [[nodiscard]] const std::vector<PlacedBuilding>& Buildings() const { return buildings_; }
        [[nodiscard]] const std::vector<PlacedTree>& Trees() const { return trees_; }
        [[nodiscard]] const std::vector<PlacedSign>& Signs() const { return signs_; }
        [[nodiscard]] const std::vector<PlacedProp>& Props() const { return props_; }
        [[nodiscard]] const std::vector<PlacedVehicle>& Vehicles() const { return vehicles_; }
        [[nodiscard]] const std::vector<PlacedSignal>& Signals() const { return signals_; }

        /// Broad-phase grids (ids index the vectors above).
        [[nodiscard]] const SpatialGrid& BuildingGrid() const { return buildingGrid_; }
        [[nodiscard]] const SpatialGrid& TreeGrid() const { return treeGrid_; }

        /// True when a point lies inside a building footprint expanded by `margin`.
        [[nodiscard]] bool InsideBuilding(const Microsoft::Xna::Framework::Vector2& point, float margin) const;

    private:
        void PlaceBuildings(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceTrees(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceAvenues(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceSigns(const MapWorld& world);
        void PlaceProps(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceVehicles(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceStreetParking(const MapWorld& world);
        void PlaceGardenTrees(const MapWorld& world);
        void PlaceMeadowTrees(const MapWorld& world);
        void PlaceDelineators(const MapWorld& world);
        /// Signal masts on the right-hand kerb of every approach to a signalised node.
        void PlaceTrafficSignals(const MapWorld& world);
        /// Front fences (picket, wire or hedge) with a gate gap along the street side of houses
        /// and cottages, side fences on cottages, and a shed behind every second one.
        void PlacePlots(const MapWorld& world);
        /// Wooden utility poles on the left side of class III, local and residential roads.
        void PlaceUtilityPoles(const MapWorld& world);
        /// Shrubs scattered along rural road verges and just outside forest polygon edges.
        void PlaceBushes(const MapWorld& world);
        void BuildGrids(const MapWorld& world);
        [[nodiscard]] bool ClearOfRoads(const MapWorld& world, const Microsoft::Xna::Framework::Vector2& p, float margin) const;

        std::vector<PlacedBuilding> buildings_;
        std::vector<PlacedTree> trees_;
        std::vector<PlacedSign> signs_;
        std::vector<PlacedProp> props_;
        std::vector<PlacedVehicle> vehicles_;
        std::vector<PlacedSignal> signals_;
        SpatialGrid buildingGrid_;
        SpatialGrid treeGrid_;
    };
}
