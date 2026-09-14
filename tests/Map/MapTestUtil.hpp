// Helpers to build small maps in memory for the map/road/lane tests.
#pragma once

#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/MapWorld.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace CarSim::Test
{
    inline Map::RoadNodeSpec Node(const std::string& id, float x, float z, bool urban = false)
    {
        Map::RoadNodeSpec n;
        n.id = id;
        n.position = Microsoft::Xna::Framework::Vector2(x, z);
        n.urban = urban;
        return n;
    }

    inline Map::RoadSpec Road(const std::string& id, std::vector<std::string> nodes, float laneWidth = 3.0f,
                              Map::RoadClass cls = Map::RoadClass::ClassIII)
    {
        Map::RoadSpec r;
        r.id = id;
        r.nodes = std::move(nodes);
        r.laneWidth = laneWidth;
        r.roadClass = cls;
        r.speedLimitKmh = 90.0f;
        return r;
    }

    inline Map::MapData FlatMap(float size = 2000.0f)
    {
        Map::MapData data;
        data.info.id = "test";
        data.info.displayName = "Test";
        data.terrain.sizeX = size;
        data.terrain.sizeZ = size;
        data.terrain.cellSize = 10.0f;
        data.terrain.noiseAmplitude = 0.0f;
        data.terrain.baseHeight = 0.0f;
        return data;
    }

    inline std::unique_ptr<Map::MapWorld> BuildWorld(Map::MapData data)
    {
        std::vector<std::string> errors;
        auto world = Map::MapWorld::Build(std::move(data), errors);
        for (const auto& e : errors) {
            ADD_FAILURE() << e;
        }
        return world;
    }

    /// Finds the lane of `road` that travels in the given direction (forward = towards +s).
    inline int FindLane(const Map::LaneGraph& lanes, const Map::RoadNetwork& roads, const std::string& roadId, bool forward, int pieceOrdinal = 0)
    {
        int ordinal = 0;
        for (const auto& lane : lanes.Lanes()) {
            if (roads.Roads()[static_cast<std::size_t>(lane.road)].spec->id != roadId || lane.forward != forward) {
                continue;
            }
            if (ordinal++ == pieceOrdinal) {
                return lane.id;
            }
        }
        return -1;
    }

    inline const Map::LaneLink* FindLink(const Map::LaneGraph& lanes, int fromLane, Map::TurnType turn)
    {
        for (const int id : lanes.LaneAt(fromLane).outgoingLinks) {
            if (lanes.LinkAt(id).turn == turn) {
                return &lanes.LinkAt(id);
            }
        }
        return nullptr;
    }
}
