#include "CarSim/Traffic/RouteDriver.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace CarSim::Traffic
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kKmhPerMs = 3.6f;

        float Clamp(const float v, const float lo, const float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        Vector2 Flat(const Vector3& v) { return Vector2(v.X, v.Z); }
    }

    RouteDriver::RouteDriver(const Map::LaneGraph& graph, RouteDriverSettings settings)
        : graph_(graph), settings_(settings)
    {
    }

    bool RouteDriver::Plan(const Vector3& position, const float headingRad, const std::vector<Vector2>& waypoints)
    {
        progress_ = RouteProgress{};
        route_.clear();
        samples_.clear();
        const int start = graph_.NearestLane(Flat(position), headingRad, 25.0f);
        if (start < 0) {
            progress_.note = "no lane within 25 m of the start";
            return false;
        }
        // A waypoint names a place on the road, not a direction of travel. Snapping it to the
        // nearest lane alone can pick the opposite carriageway, which the router then reaches by
        // driving round the whole town; so try both directions and keep the shorter route.
        std::vector<Map::RouteStep> full;
        int current = start;
        for (const auto& wp : waypoints) {
            const int nearest = graph_.NearestLane(wp, 0.0f, 60.0f);
            if (nearest < 0) {
                progress_.note = "no lane within 60 m of a waypoint";
                return false;
            }
            std::vector<int> candidates{nearest};
            const int opposite = graph_.LaneAt(nearest).oppositeLane;
            if (opposite >= 0) {
                candidates.push_back(opposite);
            }
            std::vector<Map::RouteStep> best;
            for (const int candidate : candidates) {
                if (candidate == current) {
                    best.clear();
                    break;   // already there: nothing to add for this waypoint
                }
                std::vector<Map::RouteStep> leg = graph_.FindRoute(current, candidate);
                if (leg.empty()) continue;
                if (best.empty() || leg.size() < best.size()) {
                    best = std::move(leg);
                }
            }
            if (best.empty()) {
                if (candidates.front() == current || (candidates.size() > 1 && candidates[1] == current)) {
                    continue;
                }
                progress_.note = "no route between two waypoints";
                return false;
            }
            current = best.back().lane;
            if (!full.empty()) {
                best.erase(best.begin());   // the last step of a leg is the first of the next
            }
            full.insert(full.end(), best.begin(), best.end());
        }
        if (full.empty()) {
            full.push_back(Map::RouteStep{start, -1});
        }
        route_ = std::move(full);
        Resample();
        progress_.valid = !samples_.empty();
        progress_.steps = static_cast<int>(route_.size());
        progress_.routeLengthM = samples_.empty() ? 0.0f : samples_.back().s;
        if (!progress_.valid) {
            progress_.note = "route has no geometry";
        }
        return progress_.valid;
    }

    bool RouteDriver::PlanLanes(const std::vector<int>& lanes)
    {
        progress_ = RouteProgress{};
        route_.clear();
        samples_.clear();
        if (lanes.empty()) {
            progress_.note = "empty lane list";
            return false;
        }
        for (std::size_t i = 0; i < lanes.size(); ++i) {
            int link = -1;
            if (i + 1 < lanes.size()) {
                for (const int candidate : graph_.LaneAt(lanes[i]).outgoingLinks) {
                    if (graph_.LinkAt(candidate).toLane == lanes[i + 1]) {
                        link = candidate;
                        break;
                    }
                }
                if (link < 0) {
                    progress_.note = "lanes are not connected";
                    return false;
                }
            }
            route_.push_back(Map::RouteStep{lanes[i], link});
        }
        Resample();
        progress_.valid = !samples_.empty();
        progress_.steps = static_cast<int>(route_.size());
        progress_.routeLengthM = samples_.empty() ? 0.0f : samples_.back().s;
        return progress_.valid;
    }

    void RouteDriver::Resample()
    {
        samples_.clear();
        float s = 0.0f;
        int step = 0;
        const auto append = [&](const std::vector<Map::LanePoint>& points) {
            for (std::size_t i = 0; i < points.size(); ++i) {
                Sample sample;
                sample.position = points[i].position;
                sample.tangent = points[i].tangent;
                sample.speedLimitKmh = points[i].speedLimitKmh;
                sample.curvature = std::abs(points[i].curvature);
                if (!samples_.empty()) {
                    const Vector2 d = Flat(sample.position) - Flat(samples_.back().position);
                    const float advance = std::sqrt(d.X * d.X + d.Y * d.Y);
                    if (advance < 0.05f) {
                        continue;   // duplicated joint between a lane and its link
                    }
                    s += advance;
                }
                sample.s = s;
                sample.step = step;
                samples_.push_back(sample);
            }
        };
        for (std::size_t i = 0; i < route_.size(); ++i) {
            step = static_cast<int>(i);
            if (route_[i].lane >= 0) {
                append(graph_.LaneAt(route_[i].lane).points);
            }
            if (route_[i].link >= 0) {
                append(graph_.LinkAt(route_[i].link).points);
            }
        }
    }

    float RouteDriver::TargetSpeedAt(const float s) const
    {
        // Look ahead over a braking distance and take the lowest speed the road allows there, so
        // the car is already slow when it reaches a bend rather than braking in it.
        const float horizon = 55.0f;
        float best = settings_.maxSpeedKmh;
        for (const auto& sample : samples_) {
            if (sample.s < s - 2.0f) continue;
            if (sample.s > s + horizon) break;
            float limit = sample.speedLimitKmh * settings_.speedFactor;
            if (sample.curvature > 1e-4f) {
                // v = sqrt(a / kappa): the speed at which the corner costs the allowed lateral g.
                const float radius = 1.0f / sample.curvature;
                limit = std::min(limit, std::sqrt(settings_.corneringG * 9.81f * radius) * kKmhPerMs);
            }
            // Ease the limit in with distance, so a far-away bend does not stop the car dead.
            const float ahead = std::max(0.0f, sample.s - s);
            const float relaxed = limit + ahead * 0.55f;
            best = std::min(best, relaxed);
        }
        // The last 25 m of the route are a stop.
        if (!samples_.empty()) {
            const float remaining = samples_.back().s - s;
            if (remaining < 25.0f) {
                best = std::min(best, std::max(0.0f, remaining) * 1.6f);
            }
        }
        return Clamp(best, 0.0f, settings_.maxSpeedKmh);
    }

    Sim::DriverControls RouteDriver::Update(const Sim::VehicleState& state, const float dt)
    {
        Sim::DriverControls controls;
        if (!progress_.valid || samples_.empty()) {
            controls.brake = 1.0f;
            return controls;
        }

        // Start-up: engine on, automatic in Drive. Doing this through the ordinary control
        // requests keeps the starter, the clutch and the gearbox honest.
        if (!started_) {
            startDelay_ += dt;
            if (state.engineState != Sim::EngineState::Running) {
                controls.clutch = 1.0f;
                controls.brake = 1.0f;
                // The starter is a toggle: asking every frame would switch the engine off again
                // as soon as it caught. Ask once and wait for it.
                if (!engineRequested_ && startDelay_ > 0.2f) {
                    engineRequested_ = true;
                    controls.toggleEngine = true;
                }
                return controls;
            }
            if (state.transmissionMode == Sim::TransmissionMode::Manual) {
                controls.toggleTransmissionMode = true;
                controls.brake = 1.0f;
                return controls;
            }
            controls.selector = Sim::AutomaticSelector::Drive;
            started_ = true;
        }

        // Where are we? Search forward from the cursor so a route that doubles back on itself
        // does not snap us to the wrong pass.
        const Vector2 here = Flat(state.originPosition);
        float bestDistance = 1e9f;
        std::size_t bestIndex = 0;
        for (std::size_t i = 0; i < samples_.size(); ++i) {
            // The first step searches the whole route: a spawn can sit anywhere along its lane,
            // and a window round s = 0 would never find it. Afterwards a window is enough, and
            // it keeps a route that doubles back from snapping to the wrong pass.
            if (cursorFound_ && (samples_[i].s < cursorS_ - 20.0f || samples_[i].s > cursorS_ + 80.0f)) continue;
            const Vector2 d = Flat(samples_[i].position) - here;
            const float distance = d.X * d.X + d.Y * d.Y;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        cursorS_ = samples_[bestIndex].s;
        cursorFound_ = true;
        progress_.distanceM = cursorS_;
        progress_.step = samples_[bestIndex].step;

        // Lateral error, signed positive to the right of the lane.
        const Vector3 tangent = samples_[bestIndex].tangent;
        const Vector2 toCar = here - Flat(samples_[bestIndex].position);
        progress_.lateralErrorM = toCar.X * (-tangent.Z) + toCar.Y * tangent.X;
        progress_.offRouteM = std::max(progress_.offRouteM, std::abs(progress_.lateralErrorM));

        // Pure pursuit towards a point a lookahead ahead on the route.
        const float lookahead = settings_.lookaheadM + settings_.lookaheadPerMs * std::abs(state.speedMs);
        const float targetS = cursorS_ + lookahead;
        Vector3 target = samples_.back().position;
        for (std::size_t i = bestIndex; i < samples_.size(); ++i) {
            if (samples_[i].s >= targetS) {
                target = samples_[i].position;
                break;
            }
        }
        const Vector3 forward = state.worldMatrix.getForwardProperty();
        const Vector2 f(forward.X, forward.Z);
        const Vector2 right(-f.Y, f.X);
        const Vector2 delta = Flat(target) - here;
        const float ahead = delta.X * f.X + delta.Y * f.Y;
        const float across = delta.X * right.X + delta.Y * right.Y;
        // Curvature of the arc through the target, converted to a steering command. The gain is
        // deliberately modest: this drives, it does not race.
        const float distanceSq = std::max(1.0f, ahead * ahead + across * across);
        // Pure pursuit alone under-steers out of a tight bend and drops a wheel over the edge, so
        // a cross-track term pulls the car back to the centre of its lane. The correction is
        // scaled down with speed: at 90 km/h the same gain would weave.
        const float crossTrack = -progress_.lateralErrorM * 0.22f / (1.0f + std::abs(state.speedMs) * 0.05f);
        const float command = Clamp(2.0f * across / distanceSq * 26.0f + crossTrack, -1.0f, 1.0f);
        // Smooth, so the wheel does not chatter between samples.
        const float blend = Clamp(dt * 9.0f, 0.0f, 1.0f);
        steerFilter_ += (command - steerFilter_) * blend;
        controls.steering = Clamp(steerFilter_, -1.0f, 1.0f);

        // Longitudinal: hold the target speed with a simple proportional law.
        const float targetKmh = TargetSpeedAt(cursorS_);
        progress_.targetSpeedKmh = targetKmh;
        const float errorKmh = targetKmh - state.speedKmh;
        if (errorKmh > 0.5f) {
            controls.throttle = Clamp(errorKmh * 0.09f, 0.0f, 0.85f);
        } else if (errorKmh < -1.0f) {
            controls.brake = Clamp(-errorKmh * settings_.brakeGain * 0.05f, 0.0f, 0.8f);
        }
        // An automatic creeps. At walking pace a proportional brake that light cannot hold it, so
        // the car crawled past the end of the route; the final metres are braked the way a driver
        // brings an automatic to a stop -- firmly.
        if (targetKmh < 3.0f && state.speedKmh > targetKmh) {
            controls.throttle = 0.0f;
            controls.brake = std::max(controls.brake, 0.4f);
        }
        // Back off the throttle while the wheel is hard over, which is what a driver does.
        controls.throttle *= 1.0f - 0.45f * std::abs(controls.steering);

        const float remaining = samples_.back().s - cursorS_;
        if (remaining < 3.0f && state.speedKmh < 2.0f) {
            progress_.finished = true;
            controls.throttle = 0.0f;
            controls.brake = 1.0f;
            controls.handbrake = settings_.useHandbrakeAtEnd;
        }
        return controls;
    }
}
