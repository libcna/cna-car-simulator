#include "CarSim/Map/SpatialGrid.hpp"

#include <unordered_set>

namespace CarSim::Map
{
    void SpatialGrid::Reset(const float minX, const float minZ, const float maxX, const float maxZ, const float cellSize)
    {
        minX_ = minX;
        minZ_ = minZ;
        cellSize_ = std::max(0.01f, cellSize);
        columns_ = std::max(1, static_cast<int>(std::ceil((maxX - minX) / cellSize_)));
        rows_ = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / cellSize_)));
        cells_.assign(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_), {});
    }

    void SpatialGrid::Insert(const std::int32_t id, const float minX, const float minZ, const float maxX, const float maxZ)
    {
        const int x0 = CellX(minX);
        const int x1 = CellX(maxX);
        const int z0 = CellZ(minZ);
        const int z1 = CellZ(maxZ);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(columns_) + static_cast<std::size_t>(x)].push_back(id);
            }
        }
    }

    void SpatialGrid::Query(const float minX, const float minZ, const float maxX, const float maxZ,
                            const std::function<void(std::int32_t)>& visit) const
    {
        if (cells_.empty()) {
            return;
        }
        const int x0 = CellX(minX);
        const int x1 = CellX(maxX);
        const int z0 = CellZ(minZ);
        const int z1 = CellZ(maxZ);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                for (const std::int32_t id : cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(columns_) + static_cast<std::size_t>(x)]) {
                    visit(id);
                }
            }
        }
    }

    void SpatialGrid::QueryUnique(const float minX, const float minZ, const float maxX, const float maxZ, std::vector<std::int32_t>& out) const
    {
        out.clear();
        std::unordered_set<std::int32_t> seen;
        Query(minX, minZ, maxX, maxZ, [&](const std::int32_t id) {
            if (seen.insert(id).second) {
                out.push_back(id);
            }
        });
    }

    std::size_t SpatialGrid::ItemCount() const
    {
        std::size_t n = 0;
        for (const auto& c : cells_) {
            n += c.size();
        }
        return n;
    }
}
