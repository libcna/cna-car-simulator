// Road centrelines, cross-sections and intersections derived from the authored map.
//
// Every road is a smooth curve through its nodes (Catmull-Rom in the map plane with corner
// smoothing) sampled every ~2 m. Heights come from the terrain, low-pass filtered along the
// road and pinned to the node heights so that all roads agree at intersections. Around each
// node where roads meet, the roads are trimmed back (setback) and an intersection patch
// polygon fills the gap; the remaining pieces of every road are what lanes and render strips
// are built from. Coordinates: map plane XZ, +Y up, north = -Z.
#pragma once

#include "CarSim/Map/MapData.hpp"
#include "CarSim/Map/SpatialGrid.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>
#include <string>
#include <vector>

namespace CarSim::Map
{
    using HeightSampler = std::function<float(float x, float z)>;

    struct RoadSample
    {
        Microsoft::Xna::Framework::Vector3 position{};   // centreline point (y = road surface at the crown)
        Microsoft::Xna::Framework::Vector3 tangent{};    // unit, includes slope
        float s = 0.0f;                                  // arc length from the road start (map plane)
        float curvature = 0.0f;                          // signed, 1/m, positive = turning right when travelling +s
        bool urban = false;                              // inside a built-up area (interpolated from node flags)
    };

    /// Cross-section of a road measured from the centreline outwards.
    struct RoadProfile
    {
        int lanesPerDirection = 1;
        float laneWidth = 3.0f;
        float edgeStripWidth = 0.25f;
        float shoulderWidth = 0.5f;
        float crownPercent = 2.0f;          // transverse slope of the paved surface (%)
        SidewalkSpec sidewalk;
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;

        [[nodiscard]] float HalfPavedWidth() const { return static_cast<float>(lanesPerDirection) * laneWidth + edgeStripWidth; }
        [[nodiscard]] float HalfTotalWidth() const;   // paved + shoulder or sidewalk
        [[nodiscard]] float SidewalkOuter(bool right) const;   // distance to the sidewalk's outer edge (0 if none)
    };

    class RoadCurve
    {
    public:
        void Build(const std::vector<Microsoft::Xna::Framework::Vector2>& controlPoints, const std::vector<float>& cornerRadii,
                   const std::vector<bool>& urbanFlags, float sampleSpacing, std::vector<float>& outControlS);

        [[nodiscard]] const std::vector<RoadSample>& Samples() const { return samples_; }
        [[nodiscard]] float Length() const { return samples_.empty() ? 0.0f : samples_.back().s; }
        [[nodiscard]] RoadSample Evaluate(float s) const;
        /// Nearest point on the curve in the map plane. Returns the distance in the plane;
        /// `lateral` is signed (positive = right of the travel direction, i.e. +s).
        [[nodiscard]] float Project(const Microsoft::Xna::Framework::Vector2& point, float& s, float& lateral) const;
        /// Nearest point restricted to samples in [sampleBegin, sampleEnd).
        [[nodiscard]] float ProjectRange(const Microsoft::Xna::Framework::Vector2& point, std::size_t sampleBegin, std::size_t sampleEnd,
                                         float& s, float& lateral) const;
        void SetHeights(const std::vector<float>& heights);   // one per sample; recomputes tangents

    private:
        std::vector<RoadSample> samples_;
    };

    /// A road approach at an intersection: the road leaves the node in direction `direction`.
    struct Approach
    {
        int road = -1;
        bool leavesForward = true;     // true: the road continues towards +s from the node
        float nodeS = 0.0f;            // s of the node on the road
        float setback = 0.0f;          // distance from the node centre to the road's trimmed end
        float headingRad = 0.0f;       // direction of travel away from the node (atan2(x, -z))
        Microsoft::Xna::Framework::Vector2 direction{};   // unit, map plane, away from the node
        ApproachControl control = ApproachControl::RightHandRule;
        int piece = -1;                // road piece attached to this approach
        int signalGroup = -1;          // index into the node's signal plan (-1 = not signalised)
    };

    struct Intersection
    {
        int node = -1;
        Microsoft::Xna::Framework::Vector3 center{};
        float height = 0.0f;
        std::vector<Approach> approaches;                       // sorted by heading
        std::vector<Microsoft::Xna::Framework::Vector2> patch;  // boundary polygon (map plane), counter-clockwise seen from above
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
        bool hasPriorityRoad = false;
        SignalPlan signals;            // fixed-time plan when any approach is `signal`
        float radius = 0.0f;                                    // bounding radius of the patch
        Microsoft::Xna::Framework::Vector2 gradient{};          // slope of the patch plane (dh/dx, dh/dz)

        /// Height of the (sloped) patch plane at a map-plane point.
        [[nodiscard]] float PlaneHeight(const Microsoft::Xna::Framework::Vector2& p) const
        {
            return height + gradient.X * (p.X - center.X) + gradient.Y * (p.Y - center.Z);
        }
    };

    /// Part of a road between two intersections (or a dead end).
    struct RoadPiece
    {
        int road = -1;
        float s0 = 0.0f;
        float s1 = 0.0f;
        int startIntersection = -1;    // intersection at s0 (-1 = dead end / map edge)
        int endIntersection = -1;      // intersection at s1
        std::size_t sampleBegin = 0;   // sample range covering [s0, s1] (inclusive of neighbours)
        std::size_t sampleEnd = 0;
    };

    struct Road
    {
        int index = -1;
        const RoadSpec* spec = nullptr;
        std::vector<int> nodeIndices;
        std::vector<float> nodeS;      // s of each node along the curve
        RoadCurve curve;
        RoadProfile profile;
        std::vector<int> pieces;       // indices into RoadNetwork::Pieces()

        [[nodiscard]] float SpeedLimitAt(float s) const;   // km/h, urban aware
    };

    struct RoadHit
    {
        int road = -1;
        float s = 0.0f;
        float lateral = 0.0f;          // signed, right of +s positive
        float distance = 0.0f;         // planar distance to the centreline
        RoadSample sample;             // centreline sample at s
    };

    class RoadNetwork
    {
    public:
        /// Builds curves, heights, intersections and pieces. `terrainHeight` supplies the raw
        /// terrain used for road elevation. Returns false and fills `errors` on invalid input.
        bool Build(const MapData& data, const HeightSampler& terrainHeight, std::vector<std::string>& errors);

        [[nodiscard]] const std::vector<Road>& Roads() const { return roads_; }
        [[nodiscard]] const std::vector<Intersection>& Intersections() const { return intersections_; }
        [[nodiscard]] const std::vector<RoadPiece>& Pieces() const { return pieces_; }
        [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Vector3>& NodePositions() const { return nodePositions_; }
        [[nodiscard]] int IntersectionAtNode(int nodeIndex) const;
        [[nodiscard]] float TotalLength() const;

        /// Nearest road centreline within `maxLateral` metres (planar). Prefers the road whose
        /// paved area contains the point; otherwise the closest.
        [[nodiscard]] bool NearestRoad(const Microsoft::Xna::Framework::Vector2& point, float maxLateral, RoadHit& out) const;
        /// Nearest intersection patch containing the point (-1 if none).
        [[nodiscard]] int IntersectionContaining(const Microsoft::Xna::Framework::Vector2& point) const;

        /// Surface height of a road at a signed lateral offset, including the crown.
        [[nodiscard]] float SurfaceHeight(const RoadHit& hit) const;

        /// Height/surface of the drivable road system at a point (roads and intersection patches).
        /// Returns false when the point is not on a paved or shoulder surface.
        [[nodiscard]] bool RoadSurfaceAt(const Microsoft::Xna::Framework::Vector2& point, float& height, Sim::SurfaceType& surface,
                                         float& distanceToPavedEdge) const;

        [[nodiscard]] float SampleSpacing() const { return sampleSpacing_; }

    private:
        void BuildCurves(const MapData& data, std::vector<std::string>& errors);
        void BuildHeights(const MapData& data, const HeightSampler& terrainHeight);
        void BuildIntersections(const MapData& data);
        void BuildPieces();
        void BuildGrid();
        void BuildPatch(Intersection& intersection) const;

        std::vector<Road> roads_;
        std::vector<Intersection> intersections_;
        std::vector<RoadPiece> pieces_;
        std::vector<Microsoft::Xna::Framework::Vector3> nodePositions_;
        std::vector<int> nodeIntersection_;
        SpatialGrid grid_;                 // ids = road index * 65536 + sample index (segment start)
        SpatialGrid intersectionGrid_;
        float sampleSpacing_ = 2.0f;
    };

    /// Utility: point-in-polygon in the map plane (even-odd rule).
    [[nodiscard]] bool PointInPolygon(const Microsoft::Xna::Framework::Vector2& p, const std::vector<Microsoft::Xna::Framework::Vector2>& polygon);
    /// Utility: signed area (positive = counter-clockwise in x/z with z up on paper).
    [[nodiscard]] float PolygonArea(const std::vector<Microsoft::Xna::Framework::Vector2>& polygon);
    /// Utility: planar distance from a point to a segment.
    [[nodiscard]] float DistanceToSegment(const Microsoft::Xna::Framework::Vector2& p, const Microsoft::Xna::Framework::Vector2& a,
                                          const Microsoft::Xna::Framework::Vector2& b, float& t);
}
