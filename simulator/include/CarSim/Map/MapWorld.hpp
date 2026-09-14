// Everything the simulator needs from a loaded map: authored data, road network, lane graph,
// terrain and the composite ground surface used by the vehicle physics.
#pragma once

#include "CarSim/Map/LaneGraph.hpp"
#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/ObjectPlacement.hpp"
#include "CarSim/Map/RoadNetwork.hpp"
#include "CarSim/Map/TerrainField.hpp"
#include "CarSim/Sim/Ground.hpp"

#include <memory>
#include <string>
#include <vector>

namespace CarSim::Map
{
    struct SurfaceSample
    {
        float height = 0.0f;
        Sim::SurfaceType surface = Sim::SurfaceType::Grass;
        bool onRoad = false;              // paved surface or shoulder of a road / intersection
        float distanceToPavedEdge = 0.0f; // negative inside the paved area
    };

    /// Ground surface that combines the road surfaces (authoritative where present) with the
    /// terrain height field.
    class MapGround final : public Sim::GroundSurface
    {
    public:
        MapGround(const RoadNetwork& roads, const TerrainField& terrain) : roads_(roads), terrain_(terrain) {}

        [[nodiscard]] SurfaceSample Sample(float x, float z) const;
        [[nodiscard]] float HeightAt(float x, float z) const { return Sample(x, z).height; }
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 NormalAt(float x, float z) const;

        [[nodiscard]] bool Raycast(const Microsoft::Xna::Framework::Vector3& origin,
                                   const Microsoft::Xna::Framework::Vector3& direction,
                                   float maxDistance, Sim::GroundHit& hit) const override;

    private:
        const RoadNetwork& roads_;
        const TerrainField& terrain_;
    };

    struct MapBuildStats
    {
        double loadSeconds = 0.0;
        double roadSeconds = 0.0;
        double terrainSeconds = 0.0;
        double laneSeconds = 0.0;
        double objectSeconds = 0.0;
    };

    class MapWorld
    {
    public:
        /// Loads and builds a map directory. Returns null and fills `errors` on failure.
        static std::unique_ptr<MapWorld> Load(const std::string& directory, std::vector<std::string>& errors,
                                              std::vector<std::string>* warnings = nullptr);
        /// Builds from already-parsed data (tests).
        static std::unique_ptr<MapWorld> Build(MapData data, std::vector<std::string>& errors);

        [[nodiscard]] const MapData& Data() const { return data_; }
        [[nodiscard]] const RoadNetwork& Roads() const { return roads_; }
        [[nodiscard]] const LaneGraph& Lanes() const { return lanes_; }
        [[nodiscard]] const TerrainField& Terrain() const { return terrain_; }
        [[nodiscard]] const MapGround& Ground() const { return *ground_; }
        [[nodiscard]] const ObjectPlacement& Objects() const { return objects_; }
        [[nodiscard]] const std::vector<std::string>& BuildWarnings() const { return buildWarnings_; }
        [[nodiscard]] const MapBuildStats& Stats() const { return stats_; }

        /// Player spawn: named or the first one; falls back to the first lane start.
        [[nodiscard]] SpawnSpec PlayerSpawn(const std::string& name = std::string()) const;
        /// Heading (radians, atan2(x, -z)) and position on the ground for a spawn.
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 SpawnPosition(const SpawnSpec& spawn) const;

    private:
        MapWorld() = default;

        MapData data_;
        RoadNetwork roads_;
        TerrainField terrain_;
        LaneGraph lanes_;
        std::unique_ptr<MapGround> ground_;
        ObjectPlacement objects_;
        std::vector<std::string> buildWarnings_;
        MapBuildStats stats_;
    };

    /// Heading helpers shared by map, traffic and the simulator: 0 = -Z (north), 90 deg = +X (east).
    [[nodiscard]] float HeadingFromDirection(float dx, float dz);
    [[nodiscard]] Microsoft::Xna::Framework::Vector2 DirectionFromHeading(float headingRad);
}
