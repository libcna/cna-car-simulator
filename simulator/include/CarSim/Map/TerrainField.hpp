// Height field of the map: procedural base terrain, conformed to the road network so that
// terrain and roads agree, plus region classification (meadow/field/forest/town).
#pragma once

#include "CarSim/Map/MapData.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CarSim::Map
{
    class RoadNetwork;

    class TerrainField
    {
    public:
        /// Builds the raw procedural terrain from the specification.
        void Build(const TerrainSpec& spec);

        /// Height of the raw (unconformed) procedural terrain; valid before and after Conform.
        [[nodiscard]] float RawHeight(float x, float z) const;

        /// Flattens the terrain under and around the roads (blend zone from the specification).
        void ConformToRoads(const RoadNetwork& network);

        /// Bilinear height of the final terrain; clamped at the map edge.
        [[nodiscard]] float Height(float x, float z) const;
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 Normal(float x, float z) const;
        [[nodiscard]] RegionType RegionAt(float x, float z) const;
        [[nodiscard]] const RegionSpec* RegionSpecAt(float x, float z) const;
        [[nodiscard]] bool Contains(float x, float z) const;

        // Grid access for mesh generation.
        [[nodiscard]] int Columns() const { return columns_; }     // vertices along x
        [[nodiscard]] int Rows() const { return rows_; }           // vertices along z
        [[nodiscard]] float CellSize() const { return spec_.cellSize; }
        [[nodiscard]] float MinX() const { return -spec_.sizeX * 0.5f; }
        [[nodiscard]] float MinZ() const { return -spec_.sizeZ * 0.5f; }
        [[nodiscard]] float MaxX() const { return spec_.sizeX * 0.5f; }
        [[nodiscard]] float MaxZ() const { return spec_.sizeZ * 0.5f; }
        [[nodiscard]] float HeightAtVertex(int column, int row) const { return heights_[Index(column, row)]; }
        /// Distance from the nearest road's paved edge at a vertex (large when far); set by Conform.
        [[nodiscard]] float RoadDistanceAtVertex(int column, int row) const { return roadDistance_[Index(column, row)]; }
        [[nodiscard]] const TerrainSpec& Spec() const { return spec_; }
        [[nodiscard]] float MinHeight() const { return minHeight_; }
        [[nodiscard]] float MaxHeight() const { return maxHeight_; }

    private:
        [[nodiscard]] std::size_t Index(int column, int row) const { return static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_) + static_cast<std::size_t>(column); }
        void ClassifyRegions();

        TerrainSpec spec_;
        int columns_ = 2;
        int rows_ = 2;
        std::vector<float> heights_;
        std::vector<float> roadDistance_;
        std::vector<std::uint8_t> region_;      // RegionType per vertex
        std::vector<std::int16_t> regionIndex_; // index into spec_.regions or -1
        float minHeight_ = 0.0f;
        float maxHeight_ = 0.0f;
    };
}
