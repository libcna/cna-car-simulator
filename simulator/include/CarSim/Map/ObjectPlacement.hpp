// Resolves the authored objects of a map (buildings, trees, forests, avenues, signs, props)
// into world-space placements on the terrain, plus automatically generated roadside
// delineators. Pure data: the world renderer builds meshes from it and the collision system
// builds colliders from it.
#pragma once

#include "CarSim/Map/MapData.hpp"
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
        Pine
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
    };

    struct PlacedTree
    {
        TreeSpecies species = TreeSpecies::Linden;
        Microsoft::Xna::Framework::Vector3 position{};
        float scale = 1.0f;
        float rotationRad = 0.0f;
        unsigned seed = 0;
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
        Delineator,     // Z 11a/b roadside post (generated)
        WireFence,      // wire mesh fence on steel posts (generated around plots)
        Hedge,          // clipped hedge (generated around plots)
        Shed,           // garden shed behind a house (generated)
        UtilityPole,    // wooden pole with a crossarm along village roads (generated)
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

    class ObjectPlacement
    {
    public:
        void Build(const MapWorld& world, std::vector<std::string>& warnings);

        [[nodiscard]] const std::vector<PlacedBuilding>& Buildings() const { return buildings_; }
        [[nodiscard]] const std::vector<PlacedTree>& Trees() const { return trees_; }
        [[nodiscard]] const std::vector<PlacedSign>& Signs() const { return signs_; }
        [[nodiscard]] const std::vector<PlacedProp>& Props() const { return props_; }

        /// Broad-phase grids (ids index the vectors above).
        [[nodiscard]] const SpatialGrid& BuildingGrid() const { return buildingGrid_; }
        [[nodiscard]] const SpatialGrid& TreeGrid() const { return treeGrid_; }

        /// True when a point lies inside a building footprint expanded by `margin`.
        [[nodiscard]] bool InsideBuilding(const Microsoft::Xna::Framework::Vector2& point, float margin) const;

    private:
        void PlaceBuildings(const MapWorld& world);
        void PlaceTrees(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceAvenues(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceSigns(const MapWorld& world);
        void PlaceProps(const MapWorld& world, std::vector<std::string>& warnings);
        void PlaceDelineators(const MapWorld& world);
        /// Front fences (picket, wire or hedge) with a gate gap along the street side of houses
        /// and cottages, side fences on cottages, and a shed behind every second one.
        void PlacePlots(const MapWorld& world);
        /// Wooden utility poles on the left side of class III, local and residential roads.
        void PlaceUtilityPoles(const MapWorld& world);
        void BuildGrids(const MapWorld& world);
        [[nodiscard]] bool ClearOfRoads(const MapWorld& world, const Microsoft::Xna::Framework::Vector2& p, float margin) const;

        std::vector<PlacedBuilding> buildings_;
        std::vector<PlacedTree> trees_;
        std::vector<PlacedSign> signs_;
        std::vector<PlacedProp> props_;
        SpatialGrid buildingGrid_;
        SpatialGrid treeGrid_;
    };
}
