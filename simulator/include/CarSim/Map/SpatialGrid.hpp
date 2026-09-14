// Uniform grid over the map plane storing integer item ids per cell for broad-phase queries.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace CarSim::Map
{
    class SpatialGrid
    {
    public:
        SpatialGrid() = default;
        SpatialGrid(float minX, float minZ, float maxX, float maxZ, float cellSize) { Reset(minX, minZ, maxX, maxZ, cellSize); }

        void Reset(float minX, float minZ, float maxX, float maxZ, float cellSize);

        /// Inserts `id` into every cell overlapped by the rectangle.
        void Insert(std::int32_t id, float minX, float minZ, float maxX, float maxZ);

        /// Calls `visit(id)` for every id stored in cells overlapping the rectangle (ids may repeat).
        void Query(float minX, float minZ, float maxX, float maxZ, const std::function<void(std::int32_t)>& visit) const;

        /// Collects unique ids overlapping the rectangle.
        void QueryUnique(float minX, float minZ, float maxX, float maxZ, std::vector<std::int32_t>& out) const;

        [[nodiscard]] int Columns() const { return columns_; }
        [[nodiscard]] int Rows() const { return rows_; }
        [[nodiscard]] float CellSize() const { return cellSize_; }
        [[nodiscard]] std::size_t ItemCount() const;

    private:
        [[nodiscard]] int CellX(float x) const { return std::clamp(static_cast<int>(std::floor((x - minX_) / cellSize_)), 0, columns_ - 1); }
        [[nodiscard]] int CellZ(float z) const { return std::clamp(static_cast<int>(std::floor((z - minZ_) / cellSize_)), 0, rows_ - 1); }

        float minX_ = 0.0f;
        float minZ_ = 0.0f;
        float cellSize_ = 1.0f;
        int columns_ = 1;
        int rows_ = 1;
        std::vector<std::vector<std::int32_t>> cells_;
    };
}
