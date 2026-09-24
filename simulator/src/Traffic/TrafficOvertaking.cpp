// Overtaking and return-to-lane behaviour. The state machine and clearance checks are kept
// together so changes to pass initiation cannot overlook abort and merge behaviour.
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Traffic
{
    using Map::Lane;
    using Microsoft::Xna::Framework::Vector2;

    void TrafficSystem::SetOvertakeWeather(const float wetness, const float fog, const float snowCover)
    {
        overtakeWetness_ = std::clamp(wetness, 0.0f, 1.0f);
        overtakeFog_ = std::clamp(fog, 0.0f, 1.0f);
        overtakeSnow_ = std::clamp(snowCover, 0.0f, 1.0f);
    }

    float TrafficSystem::OppositeS(const TrafficVehicle& o, const int ourLane) const
    {
        const Lane& ours = lanes_.LaneAt(ourLane);
        const Lane& theirs = lanes_.LaneAt(o.lane);
        return ours.length - o.s * (ours.length / std::max(1.0f, theirs.length));
    }

    void TrafficSystem::UpdateOvertake(TrafficVehicle& v, const Leader& leader, const float dt, float& desired, const PlayerProbe& player)
    {
        const auto settle = [&]() {
            v.lateral = std::max(0.0f, v.lateral - 1.4f * dt);
            if (v.lateral <= 0.0f) {
                v.overtaking = -1;
                v.returning = false;
            }
        };
        if (v.link >= 0 || v.lane < 0) {
            // Never carried into a junction: straighten up at once.
            v.lateral = 0.0f;
            v.overtaking = -1;
            v.returning = false;
            return;
        }
        const Lane& lane = lanes_.LaneAt(v.lane);
        const float spacing = 2.0f * std::fabs(lane.lateralOffset);
        const float toEnd = lane.length - v.s;
        const Map::RoadSpec& road = *world_.Roads().Roads()[static_cast<std::size_t>(lane.road)].spec;
        const Map::RoadPiece& piece = world_.Roads().Pieces()[static_cast<std::size_t>(lane.piece)];
        const auto roadSAt = [&](const float laneS) {
            const float t = std::clamp(laneS / std::max(1.0f, lane.length), 0.0f, 1.0f);
            return lane.forward ? piece.s0 + (piece.s1 - piece.s0) * t :
                                  piece.s1 - (piece.s1 - piece.s0) * t;
        };

        // The same IP 6 placements generate the V 7 zebra markings. Project to the lane only
        // after confirming the sign belongs to this road, so a crossing on a nearby street does
        // not veto a pass here. The small margins cover the crossing's four-metre painted span.
        const auto crossingAhead = [&](const float distance) {
            for (const auto& sign : world_.Objects().Signs()) {
                if (!sign.spec || sign.spec->code != "IP6") continue;
                const Vector2 position(sign.position.X, sign.position.Z);
                Map::RoadHit hit;
                if (!world_.Roads().NearestRoad(position, 14.0f, hit) || hit.road != lane.road) continue;
                float lateral = 0.0f;
                const float at = lane.Project(position, lateral);
                if (std::fabs(lateral) > 14.0f) continue;
                if (at - v.s >= -6.0f && at - v.s <= distance + 8.0f) return true;
            }
            return false;
        };

        // Oncoming traffic on the opposite lane, nearest first: distance ahead and speed.
        const auto oncomingClear = [&](const float required, const float seconds, const float buffer) {
            if (lane.oppositeLane < 0) return false;
            for (const auto& o : vehicles_) {
                if (o.id == v.id || o.link >= 0 || o.lane != lane.oppositeLane) continue;
                const float ahead = OppositeS(o, v.lane) - v.s;
                if (ahead > -8.0f && ahead < required + o.speed * seconds + buffer) return false;
            }
            // Traffic that will come onto the opposite lane from the junction ahead.
            for (const auto& o : vehicles_) {
                if (o.id == v.id || o.link < 0 || lanes_.LinkAt(o.link).toLane != lane.oppositeLane) continue;
                if (toEnd < required + 60.0f) return false;
            }
            if (player.valid && player.blocksTraffic && playerLane_ == lane.oppositeLane) {
                const float ahead = lane.length - playerS_ * (lane.length / std::max(1.0f, lanes_.LaneAt(playerLane_).length)) - v.s;
                if (ahead > -8.0f && ahead < required + std::max(0.0f, player.speed) * seconds + buffer + 30.0f) return false;
            }
            return true;
        };

        if (v.overtaking < 0) {
            if (v.Heavy() || lane.oppositeLane < 0 || !leader.found || leader.id < 0 || leader.onConflict ||
                road.laneWidth < 0.5f * (v.widthM + 1.0f)) return;
            const TrafficVehicle* target = FindVehicle(leader.id);
            if (!target || target->link >= 0 || target->lane != v.lane || target->overtaking >= 0) return;
            // Worth passing: a bus at a stop, a lorry or bus holding the traffic up, or a car
            // crawling along for no reason -- not one that is only slowing for a bend or a junction,
            // and not one standing in a queue: whatever holds it up would trap us alongside it, on
            // the wrong side of the road.
            const float targetToEnd = lanes_.LaneAt(target->lane).length - target->s;
            const bool crawling = target->speed > 1.5f && target->speed < 0.6f * desired && target->acceleration > -0.3f &&
                                  targetToEnd > 150.0f;
            // A bus or a lorry standing anywhere but at a stop is waiting for something (in a bend,
            // with its body across the centre line, as often as not): not one to pass.
            const bool slow = target->dwell > 0.0f || (target->Heavy() && target->speed > 1.5f && target->speed < 0.8f * desired) || crawling;
            if (!slow || leader.gap > 25.0f || desired < 12.0f) return;
            // Nothing else just ahead of it to be trapped behind.
            for (const auto& o : vehicles_) {
                if (o.id == target->id || o.id == v.id || o.link >= 0 || o.lane != v.lane) continue;
                if (o.s > target->s && o.s - target->s < target->lengthM + v.lengthM + 20.0f) return;
            }
            if (player.valid && player.blocksTraffic && playerLane_ == v.lane && playerS_ > target->s &&
                playerS_ - target->s < target->lengthM + v.lengthM + 20.0f) {
                return;
            }
            const float passLength = leader.gap + target->lengthM + v.lengthM + 14.0f;
            // Solve the distance gained on the slower car while accelerating. The old constant
            // closing speed assumed the passing car could instantly reach its desired speed,
            // which especially underestimated the time needed behind a long lorry.
            const float gripFactor = 1.0f - 0.18f * overtakeWetness_ - 0.38f * overtakeSnow_;
            const float acceleration = std::max(0.2f, params.maxAccel * v.AccelerationFactor() * gripFactor);
            const float initialClosing = std::max(0.0f, v.speed - target->speed);
            const float attainable = std::max(v.speed, desired);
            const float finalClosing = attainable - target->speed;
            if (finalClosing < 3.0f) return;
            const float accelerationTime = std::max(0.0f, attainable - v.speed) / acceleration;
            const float gainedWhileAccelerating = initialClosing * accelerationTime + 0.5f * acceleration * accelerationTime * accelerationTime;
            const float passTime = (passLength <= gainedWhileAccelerating
                ? (std::sqrt(initialClosing * initialClosing + 2.0f * acceleration * passLength) - initialClosing) / acceleration
                : accelerationTime + (passLength - gainedWhileAccelerating) / finalClosing) + 2.0f;
            if (passTime > 18.0f) return;
            // No overtaking into a junction: what counts is the road we cover while passing, not
            // the few metres we gain on it.
            const float travel = passTime * std::max(desired, v.speed) + 30.0f + 15.0f * overtakeWetness_ + 35.0f * overtakeSnow_;
            if (toEnd < travel || !road.MayOvertakeBetween(roadSAt(v.s), roadSAt(v.s + travel), lane.forward)) return;
            // In fog, the entire passing and return path must be visible before committing.
            const float sightDistance = 450.0f - 320.0f * overtakeFog_;
            if (travel > sightDistance) return;
            if (crossingAhead(travel)) return;
            // Nor into a bend: nobody passes where they cannot see round, and a bus's body swings
            // across the centre line in one.
            for (float d = 0.0f; d <= travel; d += 5.0f) {
                if (std::fabs(lane.Evaluate(v.s + d).curvature) > 1.0f / 150.0f) return;
            }
            const float buffer = 20.0f + 20.0f * overtakeWetness_ + 35.0f * overtakeSnow_ + 25.0f * overtakeFog_;
            if (!oncomingClear(travel, passTime, buffer)) return;
            v.overtaking = target->id;
            v.returning = false;
            return;
        }

        const TrafficVehicle* target = FindVehicle(v.overtaking);
        if (!target || target->link >= 0 || target->lane != v.lane) v.returning = true;
        if (!v.returning && target) {
            const float clearOfIt = target->s + 0.5f * target->lengthM + 0.5f * v.lengthM + 5.0f;
            // How far we still travel before we are past, at the speed we are gaining on it.
            const float closing = std::max(v.speed, desired) - target->speed;
            const float stillToGo = closing > 0.5f ? (clearOfIt - v.s) / closing * std::max(v.speed, desired) : 1e6f;
            if (v.s > clearOfIt) {
                v.returning = true;   // past it: back in
            } else if (!oncomingClear(clearOfIt - v.s + 20.0f,
                                      std::clamp(stillToGo / std::max(1.0f, v.speed), 1.0f, 8.0f), 12.0f)) {
                v.returning = true;   // something is coming after all: give it up
            } else if (v.s + stillToGo + 30.0f > lane.length) {
                v.returning = true;   // it will not be done before the junction (or it has sped up)
            }
        }
        if (v.returning) {
            // Pull in only where our lane is free: never sideways into the car we were passing, or
            // anybody else. Beside one, hang back behind it (it is our leader again) or, when we
            // are the further on, finish getting past.
            const TrafficVehicle* beside = nullptr;
            for (const auto& o : vehicles_) {
                if (o.id == v.id || o.link >= 0 || o.lane != v.lane || o.lateral >= 0.8f) continue;
                // Further on than it, clear of its front is enough to pull in ahead of it.
                const float margin = v.s > o.s ? 0.0f : 1.5f;
                if (std::fabs(o.s - v.s) < 0.5f * (o.lengthM + v.lengthM) + margin) {
                    beside = &o;
                    break;
                }
            }
            if (!beside) {
                settle();
            } else if (v.s > beside->s) {
                desired = std::max(desired, beside->speed + 4.0f);
            }
            return;
        }
        v.lateral = std::min(spacing, v.lateral + 1.4f * dt);
        // Pass briskly, a little over the limit, and do not dawdle alongside.
        if (target) desired = std::max(desired, std::min(desired * 1.15f, target->speed + 9.0f));
    }

}
