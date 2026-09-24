#include "CarSim/Map/MapDocument.hpp"

#include <gtest/gtest.h>

using namespace CarSim;

namespace
{
    const char* kMap = R"({"schemaVersion": 1, "id": "mini", "displayName": "Mini"})";
    const char* kTerrain = R"({"schemaVersion": 1, "size": [1000, 1000], "cellSize": 5, "noise": {"amplitude": 0}})";
    const char* kRoads = R"({"schemaVersion": 1,
        "nodes": [{"id": "a", "position": [0, 0]}, {"id": "b", "position": [0, -400], "urban": true}],
        "roads": [{"id": "r1", "class": "III", "nodes": ["a", "b"], "laneWidth": 3.0}]})";

    Map::MapSourceTexts Sources()
    {
        Map::MapSourceTexts s;
        s.map = kMap;
        s.terrain = kTerrain;
        s.roads = kRoads;
        return s;
    }
}

TEST(MapDocument, ParsesMinimalMap)
{
    const auto result = Map::ParseMapSources(Sources());
    ASSERT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front());
    EXPECT_EQ(result.data.info.id, "mini");
    ASSERT_EQ(result.data.nodes.size(), 2u);
    ASSERT_EQ(result.data.roads.size(), 1u);
    EXPECT_TRUE(result.data.nodes[1].urban);
    EXPECT_EQ(result.data.roads[0].roadClass, Map::RoadClass::ClassIII);
    EXPECT_FLOAT_EQ(result.data.terrain.sizeX, 1000.0f);
    EXPECT_FLOAT_EQ(result.data.terrain.noiseAmplitude, 0.0f);
}

TEST(MapDocument, ParsesDirectionalCentreLineAndRoadOvertakingRestriction)
{
    auto s = Sources();
    s.roads = R"({"schemaVersion": 1,
        "nodes": [{"id": "a", "position": [0, 0]}, {"id": "b", "position": [0, -400]}],
        "roads": [{"id": "r1", "class": "III", "nodes": ["a", "b"],
                   "centreLine": "solid-forward", "noOvertaking": true}]})";
    const auto result = Map::ParseMapSources(s);
    ASSERT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front());
    ASSERT_EQ(result.data.roads.size(), 1u);
    EXPECT_EQ(result.data.roads[0].centreLine, Map::CentreLineMarking::SolidForward);
    EXPECT_TRUE(result.data.roads[0].noOvertaking);
    EXPECT_FALSE(Map::MayCrossCentreLine(result.data.roads[0].centreLine, true));
    EXPECT_TRUE(Map::MayCrossCentreLine(result.data.roads[0].centreLine, false));
}

TEST(MapDocument, RejectsNewerSchemaVersion)
{
    auto s = Sources();
    s.map = R"({"schemaVersion": 2, "id": "future"})";
    const auto result = Map::ParseMapSources(s);
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.errors.front().find("schemaVersion"), std::string::npos);
}

TEST(MapDocument, ReportsUnknownNodeReference)
{
    auto s = Sources();
    s.roads = R"({"schemaVersion": 1,
        "nodes": [{"id": "a", "position": [0, 0]}],
        "roads": [{"id": "r1", "nodes": ["a", "zzz"]}]})";
    const auto result = Map::ParseMapSources(s);
    ASSERT_FALSE(result.ok());
    bool mentioned = false;
    for (const auto& e : result.errors) {
        mentioned = mentioned || e.find("zzz") != std::string::npos;
    }
    EXPECT_TRUE(mentioned);
}

TEST(MapDocument, RejectsNodesOutsideTerrain)
{
    auto s = Sources();
    s.roads = R"({"schemaVersion": 1,
        "nodes": [{"id": "a", "position": [0, 0]}, {"id": "b", "position": [5000, 0]}],
        "roads": [{"id": "r1", "nodes": ["a", "b"]}]})";
    const auto result = Map::ParseMapSources(s);
    EXPECT_FALSE(result.ok());
}

TEST(MapDocument, ReportsJsonSyntaxErrors)
{
    auto s = Sources();
    s.roads = "{ not json";
    const auto result = Map::ParseMapSources(s);
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.errors.front().find("roads.json"), std::string::npos);
}

TEST(MapDocument, ParsesIntersectionControls)
{
    auto s = Sources();
    s.roads = R"({"schemaVersion": 1,
        "nodes": [{"id": "a", "position": [-200, 0]}, {"id": "b", "position": [0, 0], "mainRoads": ["main"],
                   "control": [{"road": "minor", "control": "stop"}]},
                  {"id": "c", "position": [200, 0]}, {"id": "d", "position": [0, 200]}],
        "roads": [{"id": "main", "nodes": ["a", "b", "c"]}, {"id": "minor", "nodes": ["b", "d"]}]})";
    const auto result = Map::ParseMapSources(s);
    ASSERT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front());
    const auto* b = result.data.FindNode("b");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->mainRoads.size(), 1u);
    ASSERT_EQ(b->approachControl.count("minor"), 1u);
    EXPECT_EQ(b->approachControl.at("minor"), Map::ApproachControl::Stop);
}
