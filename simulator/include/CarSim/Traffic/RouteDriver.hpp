// A deterministic autopilot that drives the player's car along the lane graph.
//
// It exists for testing and measurement, not as a gameplay feature: benchmark runs, the
// full-map validation drive and the driving-feel scenarios all need the same car driven over
// the same ground the same way every time. It produces ordinary Sim::DriverControls, so the
// vehicle, the tyres, the gearbox and the collision world all behave exactly as they do under a
// human driver -- nothing is teleported and no physics is bypassed.
#pragma once

#include "CarSim/Map/LaneGraph.hpp"
#include "CarSim/Sim/Vehicle.hpp"

#include <string>
#include <vector>

namespace CarSim::Traffic
{
    /// How the autopilot should drive. The defaults are a careful driver: it keeps to the limit,
    /// slows for curves and comes off the throttle well before a junction.
    struct RouteDriverSettings
    {
        float speedFactor = 0.92f;        // fraction of the posted limit on the straight
        float maxSpeedKmh = 130.0f;       // hard ceiling whatever the map says
        float lookaheadM = 5.0f;          // pure-pursuit lookahead at rest
        float lookaheadPerMs = 0.45f;     // extra lookahead per m/s of speed
        float corneringG = 0.30f;         // lateral acceleration allowed through curves
        float brakeGain = 0.55f;          // how hard it brakes for an overspeed
        bool useHandbrakeAtEnd = true;    // park at the end of the route rather than rolling
    };

    /// Where the car is on its route and what it has done so far.
    struct RouteProgress
    {
        bool valid = false;               // a route was found
        bool finished = false;            // the last lane has been driven to its end
        int step = 0;                     // index into the route
        int steps = 0;
        float distanceM = 0.0f;           // travelled along the route
        float routeLengthM = 0.0f;
        float lateralErrorM = 0.0f;       // signed distance from the lane centre (+ = right)
        float targetSpeedKmh = 0.0f;
        float offRouteM = 0.0f;           // worst lateral error seen so far
        std::string note;                 // why the route is invalid, when it is
    };

    /// Drives one vehicle along a fixed sequence of lanes and links.
    class RouteDriver
    {
    public:
        RouteDriver(const Map::LaneGraph& graph, RouteDriverSettings settings = {});

        /// Plans a route from the lane nearest `position`/`heading` through the given waypoints,
        /// each of which is snapped to its nearest lane. Returns false when no route exists.
        bool Plan(const Microsoft::Xna::Framework::Vector3& position, float headingRad,
                  const std::vector<Microsoft::Xna::Framework::Vector2>& waypoints);

        /// Drives from an explicit lane sequence (each consecutive pair must be connected).
        bool PlanLanes(const std::vector<int>& lanes);

        /// One step. Returns the controls to feed to the vehicle.
        [[nodiscard]] Sim::DriverControls Update(const Sim::VehicleState& state, float dt);

        [[nodiscard]] const RouteProgress& Progress() const { return progress_; }
        [[nodiscard]] const std::vector<Map::RouteStep>& Route() const { return route_; }
        /// The centreline of the planned route, sampled every `spacing` metres. Used by the
        /// map validation drive to report where a route runs.
        [[nodiscard]] std::vector<Microsoft::Xna::Framework::Vector3> Centreline(float spacing = 5.0f) const;

    private:
        struct Sample
        {
            Microsoft::Xna::Framework::Vector3 position{};
            Microsoft::Xna::Framework::Vector3 tangent{};
            float s = 0.0f;              // distance along the whole route
            float speedLimitKmh = 50.0f;
            float curvature = 0.0f;
            int step = 0;                // which route step this sample belongs to
        };

        void Resample();
        [[nodiscard]] float TargetSpeedAt(float s) const;

        const Map::LaneGraph& graph_;
        RouteDriverSettings settings_;
        std::vector<Map::RouteStep> route_;
        std::vector<Sample> samples_;
        RouteProgress progress_;
        float cursorS_ = 0.0f;           // our best guess of where we are along the route
        bool cursorFound_ = false;       // the first step searches the whole route, not a window
        float steerFilter_ = 0.0f;       // smoothed steering command
        bool started_ = false;
        bool engineRequested_ = false;    // the starter is asked for once, not every frame
        float startDelay_ = 0.0f;
    };
}
