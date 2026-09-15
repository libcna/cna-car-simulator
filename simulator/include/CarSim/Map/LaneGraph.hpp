// Lane-level navigation graph for traffic: lanes along road pieces, connectors through
// intersections with turn types, priorities and conflict lists, and route search.
#pragma once

#include "CarSim/Map/RoadNetwork.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace CarSim::Map
{
    enum class TurnType
    {
        Straight,
        Left,
        Right,
        UTurn
    };

    struct LanePoint
    {
        Microsoft::Xna::Framework::Vector3 position{};
        Microsoft::Xna::Framework::Vector3 tangent{};
        float s = 0.0f;               // distance along the lane
        float speedLimitKmh = 50.0f;
        float curvature = 0.0f;
    };

    struct Lane
    {
        int id = -1;
        int road = -1;
        int piece = -1;
        bool forward = true;          // travels towards +s of the road
        float lateralOffset = 0.0f;   // signed offset of the lane centre from the road centreline
        float width = 3.0f;
        std::vector<LanePoint> points;
        float length = 0.0f;
        int fromIntersection = -1;    // intersection at the lane start (-1 = dead end)
        int toIntersection = -1;      // intersection at the lane end
        std::vector<int> outgoingLinks;
        std::vector<int> incomingLinks;
        int oppositeLane = -1;        // lane of the same piece in the other direction

        [[nodiscard]] LanePoint Evaluate(float s) const;
        [[nodiscard]] float Project(const Microsoft::Xna::Framework::Vector2& point, float& lateral) const;   // returns s
    };

    struct LaneLink
    {
        int id = -1;
        int fromLane = -1;
        int toLane = -1;
        int intersection = -1;
        TurnType turn = TurnType::Straight;
        ApproachControl control = ApproachControl::RightHandRule;   // control of the approach this link starts from
        bool priority = false;        // the approach has the right of way
        int signalGroup = -1;         // signal group of the approach (-1 = not signalised)
        std::vector<LanePoint> points;
        float length = 0.0f;
        std::vector<int> conflicts;   // links whose paths cross or merge with this one
        std::vector<int> yieldTo;     // subset of `conflicts` this link must give way to

        [[nodiscard]] LanePoint Evaluate(float s) const;
    };

    struct RouteStep
    {
        int lane = -1;
        int link = -1;                // link taken after the lane (-1 at the destination)
    };

    class LaneGraph
    {
    public:
        void Build(const RoadNetwork& network);

        [[nodiscard]] const std::vector<Lane>& Lanes() const { return lanes_; }
        [[nodiscard]] const std::vector<LaneLink>& Links() const { return links_; }
        [[nodiscard]] const Lane& LaneAt(int id) const { return lanes_[static_cast<std::size_t>(id)]; }
        [[nodiscard]] const LaneLink& LinkAt(int id) const { return links_[static_cast<std::size_t>(id)]; }

        /// Closest lane to a point, preferring lanes whose direction matches `headingRad`
        /// (atan2(x, -z)); returns -1 when nothing is within `maxDistance`.
        [[nodiscard]] int NearestLane(const Microsoft::Xna::Framework::Vector2& point, float headingRad, float maxDistance,
                                      float* outS = nullptr, float* outLateral = nullptr) const;

        /// Shortest route by length from one lane to another. Empty when unreachable.
        [[nodiscard]] std::vector<RouteStep> FindRoute(int fromLane, int toLane) const;

        /// Lanes reachable from `fromLane` (including itself).
        [[nodiscard]] std::vector<int> Reachable(int fromLane) const;

        /// Picks an outgoing link at random, preferring straight-through at intersections.
        [[nodiscard]] int RandomLink(int lane, std::mt19937& rng) const;

        [[nodiscard]] float TotalLaneLength() const;

    private:
        void BuildLanes(const RoadNetwork& network);
        void BuildLinks(const RoadNetwork& network);
        void BuildConflicts(const RoadNetwork& network);

        std::vector<Lane> lanes_;
        std::vector<LaneLink> links_;
        SpatialGrid grid_;   // ids = lane index * 65536 + point index
    };

    [[nodiscard]] const char* ToString(TurnType t);
}
