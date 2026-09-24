// Intersection admission: space in the box, heavy-vehicle sweep, exit clearance and
// conflicting approaches. The established priority/deadlock algorithms are unchanged.
#include "CarSim/Traffic/TrafficSystem.hpp"

#include "CarSim/Collision/Shapes.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Traffic
{
    using Map::Lane;
    using Map::LaneLink;
    using Map::LanePoint;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    bool TrafficSystem::BoxClearFor(const TrafficVehicle& v) const
    {
        if (v.nextLink < 0) {
            return true;
        }
        const LaneLink& link = lanes_.LinkAt(v.nextLink);
        // Buses and lorries have the junction to themselves: their bodies sweep outside the
        // car-sized conflict map and a stop in the mouth leaves the cab across the crossing lane.
        // A heavy vehicle enters only an empty junction, and nobody enters while one is still
        // inside, its tail included.
        if (link.intersection >= 0) {
            const auto& inter = world_.Roads().Intersections()[static_cast<std::size_t>(link.intersection)];
            const Vector2 centre(inter.center.X, inter.center.Z);
            for (const auto& o : vehicles_) {
                if (o.id == v.id) continue;
                bool inside = o.link >= 0 && lanes_.LinkAt(o.link).intersection == link.intersection;
                if (!inside && o.Heavy()) {
                    // Just out of the connector, still dragging its tail through the junction.
                    const float d = Vector2::Distance(Vector2(o.position.X, o.position.Z), centre);
                    inside = d < inter.radius + 0.5f * o.lengthM;
                }
                if (!inside) continue;
                const bool aheadOnOurConnector = o.link == v.nextLink && o.s > 0.5f * o.lengthM;
                if ((v.Heavy() || o.Heavy()) && !aheadOnOurConnector) {
                    return false;
                }
            }
        }
        // Cars that have claimed the junction on the way in count as inside -- except, for a bus
        // or a lorry already over its line, ones standing still short of it: they are waiting
        // for it (they keep clear of where its body is going), and it waiting for them locked the
        // two for good.
        const bool overTheLine = v.Heavy() && v.link < 0 && DistanceToEnd(v) < 1.0f + 0.5f * v.lengthM;
        for (const auto& o : vehicles_) {
            if (o.id == v.id || !o.claimed || o.link >= 0 || o.nextLink < 0) continue;
            if (overTheLine && o.speed < 0.5f) continue;
            const LaneLink& theirs = lanes_.LinkAt(o.nextLink);
            if (std::find(link.conflicts.begin(), link.conflicts.end(), o.nextLink) != link.conflicts.end()) return false;
            if ((v.Heavy() || o.Heavy()) && theirs.intersection == link.intersection && o.nextLink != v.nextLink) return false;
        }
        // Nobody enters while a car is still crossing on a conflicting connector: the car
        // that entered the box first gets to leave it. Entering behind its path is what locked
        // two cars nose to flank and then made the deadlock release drive one through the other.
        for (const int cId : link.conflicts) {
            const float length = lanes_.LinkAt(cId).length;
            for (const auto& o : vehicles_) {
                if (o.id != v.id && o.link == cId && o.s < length - 0.5f * o.lengthM) {
                    return false;
                }
            }
        }
        return !HeavySweepHitsStanding(v);
    }

    bool TrafficSystem::HeavySweepHitsStanding(const TrafficVehicle& v) const
    {
        // A bus or a lorry turning in a narrow junction swings its body over the end of the
        // approach next to its exit, where a car may be standing at the line. It waits at its own
        // line until its way through, body and all, is clear of anybody standing still.
        if (!v.Heavy() || v.link >= 0 || v.lane < 0 || v.nextLink < 0) return false;
        // Already over its line it cannot back out of the way: it goes, and the others keep
        // clear of where it is going (SwingHorizon).
        if (DistanceToEnd(v) < 1.0f + 0.5f * v.lengthM) return false;
        const float through = DistanceToEnd(v) + lanes_.LinkAt(v.nextLink).length + 0.5f * v.lengthM;
        for (const auto& o : vehicles_) {
            if (o.id == v.id || o.speed > 0.5f) continue;
            if (Vector3::DistanceSquared(o.position, v.position) > (through + 15.0f) * (through + 15.0f)) continue;
            if (o.link < 0 && o.lane == v.lane) continue;   // queued behind us, or the lane rules' business
            const Collision::Obb theirs = Collision::Obb::FromHeading(o.position + Vector3(0.0f, 0.5f * o.heightM, 0.0f),
                                                                      Vector3(0.5f * o.widthM, 0.5f * o.heightM, 0.5f * o.lengthM), o.headingRad);
            for (float f = 0.0f; f <= through; f += 1.5f) {
                Vector3 centre;
                float heading = 0.0f;
                if (!HeavyPoseAhead(v, f, centre, heading)) break;
                const Collision::Obb ours = Collision::Obb::FromHeading(centre + Vector3(0.0f, 0.5f * v.heightM, 0.0f),
                                                                        Vector3(0.5f * v.widthM, 0.5f * v.heightM, 0.5f * v.lengthM), heading);
                Collision::Contact contact;
                if (Collision::IntersectObbObb(ours, theirs, contact)) return true;
            }
        }
        return false;
    }

    bool TrafficSystem::ExitFreeFor(const TrafficVehicle& v) const
    {
        // Exit must be free (keep the junction box clear): no stopped car on the exit lane within
        // our own length, and no stopped car at the far end of our connector.
        if (v.nextLink < 0) return true;
        const LaneLink& link = lanes_.LinkAt(v.nextLink);
        for (const auto& o : vehicles_) {
            if (o.id == v.id || o.speed >= 0.5f) continue;
            if (o.link < 0 && o.lane == link.toLane && o.s < v.lengthM + 0.5f * o.lengthM + 3.0f) {
                return false;
            }
            if (o.link == v.nextLink && link.length - o.s < v.lengthM + 2.0f) {
                return false;
            }
        }
        return true;
    }

    bool TrafficSystem::MayEnterIntersection(const TrafficVehicle& v, const PlayerProbe& player) const
    {
        if (v.nextLink < 0) {
            return true;
        }
        const LaneLink& link = lanes_.LinkAt(v.nextLink);
        if (!ExitFreeFor(v) || !BoxClearFor(v)) {
            return false;
        }
        // A car standing on any crossing connector: the box is not clear, entering would only add
        // to the stand-off.
        for (const int cId : link.conflicts) {
            for (const auto& o : vehicles_) {
                if (o.id != v.id && o.link == cId && o.speed < 0.5f) {
                    return false;
                }
            }
        }
        // Give way to conflicting movements. The gap has to cover our own crossing as well as the
        // reaction time: a left turn across oncoming traffic takes several seconds, and a fixed
        // headway alone lets a car commit to a turn it cannot finish.
        const float entrySpeed = std::max(3.0f, v.speed);
        float crossingSeconds = std::min(6.0f, link.length / entrySpeed);
        if (v.Heavy()) {
            // A bus or a lorry has to drag its whole length clear of the crossing path and pulls
            // away slowly: it needs a much longer gap than a car does.
            crossingSeconds = std::min(8.0f, 1.2f * (link.length + v.lengthM) / entrySpeed);
        }
        // A bus or a lorry over its line already, or one that has claimed the junction after a
        // long wait (UpdateVehicle), no longer waits for anybody standing at theirs: they are
        // waiting for it.
        const bool claimedIt = v.Heavy() && v.claimed && v.standingAtLine > 2.0f * params.deadlockSeconds;
        const bool overTheLine = v.Heavy() && v.link < 0 && (DistanceToEnd(v) < 1.0f + 0.5f * v.lengthM || claimedIt);
        // A bus or a lorry also waits for anything arriving fast from any approach: its body can
        // reach paths the car-sized conflict map does not list.
        if (v.Heavy() && link.intersection >= 0) {
            for (const auto& o : vehicles_) {
                if (o.id == v.id || o.link >= 0 || o.nextLink < 0 || o.lane == v.lane) continue;
                if (overTheLine && o.speed < 0.5f) continue;
                const LaneLink& theirs = lanes_.LinkAt(o.nextLink);
                if (theirs.intersection != link.intersection) continue;
                if (theirs.control == Map::ApproachControl::Signal && theirs.signalGroup >= 0 &&
                    AspectOf(theirs.intersection, theirs.signalGroup) != SignalAspect::Green) continue;   // held at its red
                const float remaining = lanes_.LaneAt(o.lane).length - o.s;
                const float eta = remaining / std::max(1.0f, o.speed);
                // Having claimed the junction, it waits only for those too close to stop for the
                // claim; the rest hold at their lines (on a busy green there was always one more
                // on its way, and the bus waited cycle after cycle).
                if (claimedIt && remaining > o.speed * o.speed / (2.0f * params.comfortDecel) + 3.0f) continue;
                if (remaining < 6.0f || (o.speed > 0.8f && eta < params.yieldTimeGap + crossingSeconds)) {
                    return false;
                }
            }
        }
        for (const int mId : link.yieldTo) {
            const LaneLink& m = lanes_.LinkAt(mId);
            // At a signal only the movements that have green matter; the lane graph lists the
            // cross streams too, and waiting for cars standing at their red line never ends.
            const bool heldBySignal = m.control == Map::ApproachControl::Signal && m.signalGroup >= 0 &&
                                      AspectOf(m.intersection, m.signalGroup) != SignalAspect::Green;
            for (const auto& o : vehicles_) {
                if (o.id == v.id) continue;
                if (o.link == mId) {
                    return false;   // already crossing
                }
                if (o.link < 0 && o.lane == m.fromLane && o.nextLink == mId && !heldBySignal && !(overTheLine && o.speed < 0.5f)) {
                    const float remaining = lanes_.LaneAt(o.lane).length - o.s;
                    const float eta = remaining / std::max(1.0f, o.speed);
                    // Having claimed the junction (a bus or a lorry after a long wait), only those
                    // too close to stop for the claim still count.
                    if (claimedIt && remaining > o.speed * o.speed / (2.0f * params.comfortDecel) + 3.0f) continue;
                    if (remaining < 6.0f || (o.speed > 0.8f && eta < params.yieldTimeGap + crossingSeconds)) {
                        return false;
                    }
                }
                // Still in the previous junction on its way onto the approach lane: it has not
                // chosen its next connector yet, so assume it may be coming our way. Without this
                // a short approach lane hides a priority car until it is too close to give way.
                if (o.link >= 0 && lanes_.LinkAt(o.link).toLane == m.fromLane && !heldBySignal && !claimedIt) {
                    const float remaining = lanes_.LinkAt(o.link).length - o.s + lanes_.LaneAt(m.fromLane).length;
                    const float eta = remaining / std::max(1.0f, o.speed);
                    if (o.speed > 0.8f && eta < params.yieldTimeGap + crossingSeconds) {
                        return false;
                    }
                }
            }
            // The player approaching on the conflicting lane, or already inside the junction.
            if (player.valid && player.blocksTraffic && playerLane_ == m.fromLane) {
                const Lane& lane = lanes_.LaneAt(playerLane_);
                const float remaining = lane.length - playerS_;
                const LanePoint lp = lane.Evaluate(playerS_);
                const float along = Vector3::Dot(player.forward * player.speed, lp.tangent);
                const float eta = remaining / std::max(1.0f, along);
                if (remaining < 8.0f || (along > 0.8f && eta < params.yieldTimeGap + crossingSeconds)) {
                    return false;
                }
            }
        }
        // Priority movements still avoid a player standing in the junction on a crossing path.
        if (player.valid && player.blocksTraffic && link.intersection >= 0) {
            const auto& inter = world_.Roads().Intersections()[static_cast<std::size_t>(link.intersection)];
            const float d = Vector2::Distance(Vector2(player.position.X, player.position.Z), Vector2(inter.center.X, inter.center.Z));
            if (d < inter.radius + 1.0f && std::fabs(player.speed) < 1.0f) {
                float lateral = 0.0f;
                (void)lateral;
                for (std::size_t i = 0; i + 1 < link.points.size(); ++i) {
                    float t = 0.0f;
                    if (Map::DistanceToSegment(Vector2(player.position.X, player.position.Z), Vector2(link.points[i].position.X, link.points[i].position.Z),
                                               Vector2(link.points[i + 1].position.X, link.points[i + 1].position.Z), t) < 2.5f) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // ------------------------------------------------------------------ update

}
