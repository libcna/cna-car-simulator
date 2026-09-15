#include "CarSim/Traffic/TrafficSystem.hpp"

#include "CarSim/Collision/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace CarSim::Traffic
{
    using Map::Lane;
    using Map::LaneLink;
    using Map::LanePoint;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kKmhToMs = 1.0f / 3.6f;
        constexpr float kWheelRadius = 0.31f;
    }

    Matrix TrafficVehicle::WorldMatrix() const
    {
        // Yaw about +Y: heading is clockwise from north, the XNA rotation is counter-clockwise.
        return Matrix::CreateRotationY(-headingRad) * Matrix::CreateTranslation(position);
    }

    TrafficSystem::TrafficSystem(const Map::MapWorld& world, const std::uint32_t seed)
        : world_(world), lanes_(world.Lanes()), rng_(seed), plates_(seed * 7919u + 13u)
    {
        maxVehicles_ = world.Data().traffic.maxVehicles;
    }

    float TrafficSystem::IdmAcceleration(const float speed, const float desiredSpeed, const float gap, const float leaderSpeed, const TrafficParams& p)
    {
        const float v0 = std::max(0.5f, desiredSpeed);
        const float free = 1.0f - std::pow(std::max(0.0f, speed) / v0, 4.0f);
        if (gap >= 1e8f) {
            return p.maxAccel * free;
        }
        const float dv = speed - leaderSpeed;
        const float sStar = p.minGap + std::max(0.0f, speed * p.timeHeadway + speed * dv / (2.0f * std::sqrt(p.maxAccel * p.comfortDecel)));
        const float interaction = std::pow(sStar / std::max(0.2f, gap), 2.0f);
        return p.maxAccel * (free - interaction);
    }

    // ------------------------------------------------------------------ helpers

    float TrafficSystem::DistanceToEnd(const TrafficVehicle& v) const
    {
        if (v.link >= 0) {
            return lanes_.LinkAt(v.link).length - v.s;
        }
        if (v.lane >= 0) {
            return lanes_.LaneAt(v.lane).length - v.s;
        }
        return 0.0f;
    }

    bool TrafficSystem::PathPointAhead(const TrafficVehicle& v, const float ahead, LanePoint& out) const
    {
        if (v.link >= 0) {
            const LaneLink& link = lanes_.LinkAt(v.link);
            const float s = v.s + ahead;
            if (s <= link.length) {
                out = link.Evaluate(s);
                return true;
            }
            const Lane& next = lanes_.LaneAt(link.toLane);
            if (s - link.length <= next.length) {
                out = next.Evaluate(s - link.length);
                return true;
            }
            return false;
        }
        if (v.lane < 0) {
            return false;
        }
        const Lane& lane = lanes_.LaneAt(v.lane);
        float s = v.s + ahead;
        if (s <= lane.length) {
            out = lane.Evaluate(s);
            return true;
        }
        if (v.nextLink < 0) {
            return false;
        }
        const LaneLink& link = lanes_.LinkAt(v.nextLink);
        s -= lane.length;
        if (s <= link.length) {
            out = link.Evaluate(s);
            return true;
        }
        const Lane& next = lanes_.LaneAt(link.toLane);
        s -= link.length;
        if (s <= next.length) {
            out = next.Evaluate(s);
            return true;
        }
        return false;
    }

    float TrafficSystem::PathBlockedBy(const TrafficVehicle& v, const TrafficVehicle& other, const float maxAhead) const
    {
        const Collision::Obb box = Collision::Obb::FromHeading(other.position + Vector3(0.0f, 0.5f * other.heightM, 0.0f),
                                                               Vector3(0.5f * other.widthM, 0.5f * other.heightM, 0.5f * other.lengthM),
                                                               other.headingRad);
        const float margin = 0.5f * v.widthM + 0.2f;
        for (float d = 0.0f; d <= maxAhead; d += 0.75f) {
            LanePoint p;
            if (!PathPointAhead(v, d, p)) break;
            Vector3 q = p.position;
            q.Y = box.centre.Y;
            if (box.Contains(q, margin)) {
                return d;
            }
        }
        return -1.0f;
    }

    const TrafficVehicle* TrafficSystem::FindVehicle(const int id) const
    {
        for (const auto& o : vehicles_) {
            if (o.id == id) return &o;
        }
        return nullptr;
    }

    bool TrafficSystem::LaneOccupiedNear(const int lane, const float s, const float radius, const int ignoreId) const
    {
        for (const auto& o : vehicles_) {
            if (o.id == ignoreId || o.lane != lane || o.link >= 0) continue;
            if (std::fabs(o.s - s) < radius) return true;
        }
        return false;
    }

    void TrafficSystem::ChooseNextLink(TrafficVehicle& v)
    {
        v.nextLink = v.lane >= 0 ? lanes_.RandomLink(v.lane, rng_) : -1;
    }

    void TrafficSystem::UpdatePose(TrafficVehicle& v)
    {
        LanePoint p;
        if (v.link >= 0) {
            p = lanes_.LinkAt(v.link).Evaluate(v.s);
        } else if (v.lane >= 0) {
            p = lanes_.LaneAt(v.lane).Evaluate(v.s);
        } else {
            return;
        }
        v.position = p.position;
        Vector3 f(p.tangent.X, 0.0f, p.tangent.Z);
        if (f.LengthSquared() > 1e-8f) {
            f.Normalize();
            v.forward = f;
            v.headingRad = std::atan2(f.X, -f.Z);
        }
        // Front wheels follow the path curvature: steer angle ~ atan(wheelbase * curvature).
        v.steerAngle = std::atan(2.55f * p.curvature);
    }

    float TrafficSystem::DesiredSpeedAhead(const TrafficVehicle& v) const
    {
        // Minimum of the speed limit and the curve speed over the next 40 m, and the target
        // speed of the coming link (turns are slow) weighted by distance.
        float desired = 130.0f * kKmhToMs;
        const auto consider = [&](const LanePoint& p, const float distance) {
            float limit = p.speedLimitKmh * kKmhToMs * v.driverFactor;
            const float curvature = std::fabs(p.curvature);
            if (curvature > 1e-4f) {
                limit = std::min(limit, std::sqrt(params.maxLateralAccel / curvature));
            }
            // Speeds further ahead only matter if we could not brake down to them in time.
            const float brakeSpeed = std::sqrt(std::max(0.0f, limit * limit + 2.0f * params.comfortDecel * distance));
            desired = std::min(desired, std::max(limit, brakeSpeed));
        };
        if (v.link >= 0) {
            const LaneLink& link = lanes_.LinkAt(v.link);
            for (float d = 0.0f; d < 40.0f; d += 5.0f) {
                const float s = v.s + d;
                if (s <= link.length) {
                    consider(link.Evaluate(s), d);
                } else {
                    const Lane& next = lanes_.LaneAt(link.toLane);
                    consider(next.Evaluate(s - link.length), d);
                }
            }
        } else if (v.lane >= 0) {
            const Lane& lane = lanes_.LaneAt(v.lane);
            for (float d = 0.0f; d < 60.0f; d += 5.0f) {
                const float s = v.s + d;
                if (s <= lane.length) {
                    consider(lane.Evaluate(s), d);
                } else if (v.nextLink >= 0) {
                    const LaneLink& link = lanes_.LinkAt(v.nextLink);
                    const float ls = s - lane.length;
                    if (ls <= link.length) {
                        consider(link.Evaluate(ls), d);
                    }
                }
            }
        }
        return desired;
    }

    TrafficSystem::Leader TrafficSystem::FindLeader(const TrafficVehicle& v, const PlayerProbe& player) const
    {
        // Path elements ahead: (isLink, id, sStart in element, offset from our position).
        struct Segment { bool isLink; int id; float sFrom; float offset; float length; };
        std::vector<Segment> path;
        float lookahead = 90.0f;
        float offset = 0.0f;
        if (v.link >= 0) {
            const LaneLink& link = lanes_.LinkAt(v.link);
            path.push_back({true, v.link, v.s, 0.0f, link.length});
            offset = link.length - v.s;
            const Lane& next = lanes_.LaneAt(link.toLane);
            path.push_back({false, link.toLane, 0.0f, offset, next.length});
        } else if (v.lane >= 0) {
            const Lane& lane = lanes_.LaneAt(v.lane);
            path.push_back({false, v.lane, v.s, 0.0f, lane.length});
            offset = lane.length - v.s;
            if (v.nextLink >= 0) {
                const LaneLink& link = lanes_.LinkAt(v.nextLink);
                path.push_back({true, v.nextLink, 0.0f, offset, link.length});
                offset += link.length;
                const Lane& next = lanes_.LaneAt(link.toLane);
                path.push_back({false, link.toLane, 0.0f, offset, next.length});
            }
        }
        Leader best;
        const auto consider = [&](const float distanceAhead, const float otherLength, const float otherSpeed, const int id, const bool onConflict) {
            const float gap = distanceAhead - otherLength * 0.5f - v.lengthM * 0.5f;
            if (gap < best.gap) {
                best.found = true;
                best.gap = gap;
                best.speed = otherSpeed;
                best.id = id;
                best.onConflict = onConflict;
            }
        };
        for (const auto& o : vehicles_) {
            if (o.id == v.id) continue;
            for (const auto& seg : path) {
                const bool same = seg.isLink ? (o.link == seg.id) : (o.link < 0 && o.lane == seg.id);
                if (!same) continue;
                if (o.s < seg.sFrom - 0.5f) continue;
                const float ahead = seg.offset + (o.s - seg.sFrom);
                if (ahead > lookahead) continue;
                consider(ahead, o.lengthM, o.speed, o.id, false);
            }
        }
        // A car that has just left our lane onto a connector is still physically in front of us,
        // whichever way it turned. Without this a follower can drive into the back of a car that
        // entered the junction on a different connector from the same lane.
        if (v.link < 0 && v.lane >= 0) {
            const Lane& lane = lanes_.LaneAt(v.lane);
            const float toEnd = lane.length - v.s;
            if (toEnd < 30.0f) {
                for (const auto& o : vehicles_) {
                    if (o.id == v.id || o.link < 0 || o.link == v.nextLink) continue;
                    if (lanes_.LinkAt(o.link).fromLane != v.lane) continue;
                    const float ahead = toEnd + o.s;
                    if (ahead > 0.0f && ahead <= lookahead) {
                        consider(ahead, o.lengthM, o.speed, o.id, true);
                    }
                }
            }
        }
        // The player: only when it drives along one of our path lanes in our direction.
        if (player.valid && playerLane_ >= 0) {
            for (const auto& seg : path) {
                if (seg.isLink || seg.id != playerLane_) continue;
                if (playerS_ < seg.sFrom - 0.5f) continue;
                const float ahead = seg.offset + (playerS_ - seg.sFrom);
                if (ahead > lookahead) continue;
                const Lane& lane = lanes_.LaneAt(playerLane_);
                const LanePoint lp = lane.Evaluate(playerS_);
                const float along = Vector3::Dot(player.forward * player.speed, lp.tangent);
                consider(ahead, player.lengthM, std::max(0.0f, along), -1, false);
            }
        }
        // Inside a junction (or about to enter one): a car on a crossing connector whose body sits
        // on our path is an obstacle whatever the priority, so nobody is ever driven through.
        {
            const LaneLink* box = nullptr;
            if (v.link >= 0) {
                box = &lanes_.LinkAt(v.link);
            } else if (v.nextLink >= 0 && DistanceToEnd(v) < 20.0f) {
                box = &lanes_.LinkAt(v.nextLink);
            }
            if (box) {
                for (const auto& o : vehicles_) {
                    if (o.id == v.id || o.link < 0) continue;
                    if (std::find(box->conflicts.begin(), box->conflicts.end(), o.link) == box->conflicts.end()) continue;
                    if (Vector3::DistanceSquared(o.position, v.position) > 30.0f * 30.0f) continue;
                    const float blockedAt = PathBlockedBy(v, o, 24.0f);
                    if (blockedAt < 0.0f) continue;
                    // The sample lies at the other car's boundary: measure from our front bumper.
                    const float distanceAhead = blockedAt + 0.5f * o.lengthM;
                    consider(distanceAhead, o.lengthM, std::max(0.0f, Vector3::Dot(o.Velocity(), v.forward)), o.id, true);
                }
            }
        }
        // Also treat the player as an obstacle when it physically sits on our path (any heading).
        if (player.valid) {
            for (const auto& seg : path) {
                if (seg.isLink) continue;
                const Lane& lane = lanes_.LaneAt(seg.id);
                float lateral = 0.0f;
                const float ps = lane.Project(Vector2(player.position.X, player.position.Z), lateral);
                if (std::fabs(lateral) > 1.6f || ps < seg.sFrom - 0.5f) continue;
                const float ahead = seg.offset + (ps - seg.sFrom);
                if (ahead > lookahead) continue;
                const LanePoint lp = lane.Evaluate(ps);
                const float along = Vector3::Dot(player.forward * player.speed, lp.tangent);
                consider(ahead, player.lengthM, std::max(0.0f, along), -1, false);
            }
        }
        return best;
    }

    bool TrafficSystem::MayEnterIntersection(const TrafficVehicle& v, const PlayerProbe& player) const
    {
        if (v.nextLink < 0) {
            return true;
        }
        const LaneLink& link = lanes_.LinkAt(v.nextLink);
        // Exit must be free (keep the junction box clear): no stopped car on the exit lane within
        // our own length, and no stopped car at the far end of our connector.
        for (const auto& o : vehicles_) {
            if (o.id == v.id || o.speed >= 0.5f) continue;
            if (o.link < 0 && o.lane == link.toLane && o.s < v.lengthM + 0.5f * o.lengthM + 3.0f) {
                return false;
            }
            if (o.link == v.nextLink && link.length - o.s < v.lengthM + 2.0f) {
                return false;
            }
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
        const float crossingSeconds = std::min(6.0f, link.length / entrySpeed);
        for (const int mId : link.yieldTo) {
            const LaneLink& m = lanes_.LinkAt(mId);
            for (const auto& o : vehicles_) {
                if (o.id == v.id) continue;
                if (o.link == mId) {
                    return false;   // already crossing
                }
                if (o.link < 0 && o.lane == m.fromLane && o.nextLink == mId) {
                    const float remaining = lanes_.LaneAt(o.lane).length - o.s;
                    const float eta = remaining / std::max(1.0f, o.speed);
                    if (remaining < 6.0f || (o.speed > 0.8f && eta < params.yieldTimeGap + crossingSeconds)) {
                        return false;
                    }
                }
            }
            // The player approaching on the conflicting lane, or already inside the junction.
            if (player.valid && playerLane_ == m.fromLane) {
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
        if (player.valid && link.intersection >= 0) {
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

    void TrafficSystem::UpdateVehicle(TrafficVehicle& v, const float dt, const PlayerProbe& player)
    {
        v.age += dt;
        if (v.backingOff) {
            // Reverse along the connector at walking pace until back at the line.
            v.speed = 0.0f;
            v.acceleration = 0.0f;
            v.brakeLights = true;
            v.s -= 1.2f * dt;
            v.wheelSpin -= 1.2f * dt / kWheelRadius;
            if (v.s <= 0.0f && v.link >= 0) {
                const LaneLink& link = lanes_.LinkAt(v.link);
                v.lane = link.fromLane;
                v.nextLink = v.link;
                v.link = -1;
                v.s = std::max(0.0f, lanes_.LaneAt(v.lane).length - 0.05f);
                v.backingOff = false;
                v.committed = false;
                v.stoppedAtLine = true;
                v.waiting = true;
                v.waitTime = 0.0f;
            }
            UpdatePose(v);
            return;
        }
        const float distanceToEnd = DistanceToEnd(v);
        float desired = DesiredSpeedAhead(v);
        Leader leader = FindLeader(v, player);
        float gap = leader.found ? leader.gap : 1e9f;
        float leaderSpeed = leader.found ? leader.speed : 0.0f;

        // Stand-off inside a junction: stopped nose to nose with a car on a crossing connector that
        // is stopped as well. After a while the car without right of way (or, when neither yields,
        // the one that arrived later) backs out to its line so the other can pass.
        if (v.link >= 0) {
            const bool standing = v.speed < 0.05f && leader.found && leader.onConflict && leader.speed < 0.05f;
            v.standoffTime = standing ? v.standoffTime + dt : 0.0f;
            const TrafficVehicle* other = standing && v.standoffTime > params.deadlockSeconds ? FindVehicle(leader.id) : nullptr;
            if (other && other->link >= 0) {
                const LaneLink& mine = lanes_.LinkAt(v.link);
                const LaneLink& theirs = lanes_.LinkAt(other->link);
                const bool iYield = std::find(mine.yieldTo.begin(), mine.yieldTo.end(), theirs.id) != mine.yieldTo.end();
                const bool theyYield = std::find(theirs.yieldTo.begin(), theirs.yieldTo.end(), mine.id) != theirs.yieldTo.end();
                bool roomBehind = true;
                for (const auto& o : vehicles_) {
                    if (o.id == v.id) continue;
                    if ((o.link == v.link && o.s < v.s) || (o.link < 0 && o.lane == mine.fromLane && lanes_.LaneAt(o.lane).length - o.s < v.s + 8.0f)) {
                        roomBehind = false;
                    }
                }
                if (roomBehind && (iYield || (!theyYield && v.id > other->id))) {
                    v.backingOff = true;
                    v.standoffTime = 0.0f;
                }
            }
        } else {
            v.standoffTime = 0.0f;
        }

        // Last resort inside the box: a car that has stood still on a connector for half again the
        // deadlock time creeps out of the junction. Only the lower id of a pair moves, so two cars
        // nose to nose separate instead of driving through each other, and only when nothing is
        // queued ahead on its own connector. Without this a pair that entered together can stand
        // there for the rest of the session: the back-off above needs room behind, and there is
        // not always any.
        bool clearTheBox = false;
        if (v.link >= 0) {
            v.blockedTime = v.speed < 0.3f ? v.blockedTime + dt : 0.0f;
            const float releaseAfter = params.deadlockSeconds * 1.5f;
            if (v.clearingBox) {
                clearTheBox = true;
            } else if (v.blockedTime > releaseAfter) {
                bool queuedAhead = false;
                int lowestBlockedId = v.id;
                for (const auto& o : vehicles_) {
                    if (o.id == v.id) continue;
                    if (o.link == v.link && o.s > v.s && o.s - v.s < v.lengthM + 6.0f) queuedAhead = true;
                    if (o.link >= 0 && o.link != v.link && o.blockedTime > releaseAfter &&
                        Vector3::Distance(o.position, v.position) < 12.0f) {
                        lowestBlockedId = std::min(lowestBlockedId, o.id);
                    }
                }
                clearTheBox = !queuedAhead && lowestBlockedId == v.id;
                v.clearingBox = clearTheBox;
            }
        } else {
            v.blockedTime = 0.0f;
            v.clearingBox = false;
        }

        // Intersection control at the end of a lane.
        v.indicatorLeft = v.indicatorRight = false;
        if (v.link < 0 && v.nextLink >= 0) {
            const LaneLink& link = lanes_.LinkAt(v.nextLink);
            if (distanceToEnd < 45.0f) {
                v.indicatorLeft = link.turn == Map::TurnType::Left || link.turn == Map::TurnType::UTurn;
                v.indicatorRight = link.turn == Map::TurnType::Right;
            }
            // Signals first: a red light overrides every priority rule, and a green one means
            // the cross traffic is being held, so only the turning conflicts inside the junction
            // still matter.
            SignalAspect aspect = SignalAspect::Green;
            bool signalised = false;
            bool signalHold = false;
            if (link.control == Map::ApproachControl::Signal && link.signalGroup >= 0) {
                aspect = AspectOf(link.intersection, link.signalGroup);
                signalised = true;
            }
            if (signalised && distanceToEnd < 60.0f && !v.committed) {
                // Amber means stop unless that would mean braking harder than a normal stop, in
                // which case the car is already too close and carries on.
                const float comfortableStop = v.speed * v.speed / (2.0f * 3.0f) + 1.0f;
                const bool mustStop = aspect == SignalAspect::Red || aspect == SignalAspect::RedAmber ||
                                      (aspect == SignalAspect::Amber && distanceToEnd > comfortableStop);
                if (mustStop) {
                    signalHold = true;
                    v.waiting = true;
                    v.waitTime += dt;
                    v.stoppedAtLine = false;
                    const float lineGap = distanceToEnd - 1.0f - v.lengthM * 0.5f;
                    if (lineGap < gap) {
                        gap = std::max(0.05f, lineGap);
                        leaderSpeed = 0.0f;
                    }
                } else if (distanceToEnd < 12.0f) {
                    v.committed = true;   // through on green: do not stop halfway on a change
                }
            }
            const bool controlled = !signalised &&
                                    (link.control == Map::ApproachControl::Yield || link.control == Map::ApproachControl::Stop ||
                                     link.control == Map::ApproachControl::RightHandRule || !link.yieldTo.empty());
            if (controlled && distanceToEnd < 40.0f) {
                bool hold = false;
                if (link.control == Map::ApproachControl::Stop && !v.stoppedAtLine) {
                    // Come to a full stop at the line first.
                    hold = true;
                    if (distanceToEnd < 2.5f && v.speed < 0.3f) {
                        v.stoppedAtLine = true;
                    }
                }
                if (!hold && !v.committed && !MayEnterIntersection(v, player)) {
                    hold = true;
                }
                if (hold) {
                    v.waiting = true;
                    v.waitTime += dt;
                    // Deadlock breaker: after a long wait with nothing moving inside the junction,
                    // the car that has waited longest commits and goes; the others keep waiting
                    // until it is through (the released car stays committed until it is on the link).
                    if (v.waitTime > params.deadlockSeconds) {
                        bool anyoneInside = false;
                        bool someoneElseFirst = false;
                        for (const auto& o : vehicles_) {
                            if (o.id == v.id) continue;
                            if (o.link >= 0 && lanes_.LinkAt(o.link).intersection == link.intersection &&
                                (o.speed > 0.3f || std::find(link.conflicts.begin(), link.conflicts.end(), o.link) != link.conflicts.end())) {
                                anyoneInside = true;
                            }
                            if (o.link < 0 && o.nextLink >= 0 && lanes_.LinkAt(o.nextLink).intersection == link.intersection) {
                                if (o.committed || o.waitTime > v.waitTime || (o.waitTime == v.waitTime && o.id < v.id)) {
                                    someoneElseFirst = true;
                                }
                            }
                        }
                        if (!anyoneInside && !someoneElseFirst) {
                            hold = false;
                            v.committed = true;
                        }
                    }
                }
                if (hold) {
                    // Stop line as a standing obstacle just before the lane end.
                    const float lineGap = distanceToEnd - 1.0f - v.lengthM * 0.5f;
                    if (lineGap < gap) {
                        gap = std::max(0.05f, lineGap);
                        leaderSpeed = 0.0f;
                    }
                } else {
                    v.waiting = false;
                    v.waitTime = 0.0f;
                }
            } else if (!signalHold) {
                v.waiting = false;
                v.waitTime = 0.0f;
            }
        }
        if (v.stunned > 0.0f) {
            v.stunned -= dt;
            desired = 0.0f;
        }

        if (clearTheBox) {
            desired = std::min(desired, 3.0f);
            gap = 1e9f;
            leaderSpeed = 0.0f;
            v.committed = true;
        }
        float accel = IdmAcceleration(v.speed, desired, gap, leaderSpeed, params);
        accel = std::clamp(accel, -8.0f, params.maxAccel);
        v.acceleration = accel;
        const float newSpeed = std::max(0.0f, v.speed + accel * dt);
        v.brakeLights = accel < -0.6f || (v.stunned > 0.0f && v.speed > 0.1f);
        const float distance = 0.5f * (v.speed + newSpeed) * dt;
        v.speed = newSpeed;
        v.wheelSpin += distance / kWheelRadius;
        v.s += distance;

        // Element transitions.
        for (int guard = 0; guard < 4; ++guard) {
            if (v.link >= 0) {
                const LaneLink& link = lanes_.LinkAt(v.link);
                if (v.s < link.length) break;
                v.s -= link.length;
                v.lane = link.toLane;
                v.link = -1;
                v.stoppedAtLine = false;
                v.waiting = false;
                v.waitTime = 0.0f;
                ChooseNextLink(v);
            } else if (v.lane >= 0) {
                const Lane& lane = lanes_.LaneAt(v.lane);
                if (v.s < lane.length) break;
                if (v.nextLink < 0) {
                    ChooseNextLink(v);
                }
                if (v.nextLink < 0) {
                    v.s = lane.length;   // dead end without links: stay
                    v.speed = 0.0f;
                    break;
                }
                v.s -= lane.length;
                v.link = v.nextLink;
                v.nextLink = -1;
                v.committed = false;
            } else {
                break;
            }
        }
        UpdatePose(v);
    }

    SignalAspect TrafficSystem::AspectOf(const int intersection, const int group) const
    {
        const auto& intersections = world_.Roads().Intersections();
        if (intersection < 0 || static_cast<std::size_t>(intersection) >= intersections.size()) {
            return SignalAspect::Green;
        }
        return signals_.Aspect(intersections[static_cast<std::size_t>(intersection)].signals, group);
    }

    void TrafficSystem::Update(const float dt, const PlayerProbe& player)
    {
        if (dt <= 0.0f) {
            return;
        }
        // Project the player onto the lane graph once per update.
        playerLane_ = -1;
        if (player.valid) {
            const float heading = std::atan2(player.forward.X, -player.forward.Z);
            playerLane_ = lanes_.NearestLane(Vector2(player.position.X, player.position.Z), heading, 4.0f, &playerS_);
        }
        signals_.Update(dt);
        Despawn(player);
        SpawnAroundPlayer(player);
        for (auto& v : vehicles_) {
            UpdateVehicle(v, dt, player);
        }
    }

    void TrafficSystem::NotifyCollision(const int id, const float seconds)
    {
        for (auto& v : vehicles_) {
            if (v.id == id) {
                v.stunned = std::max(v.stunned, seconds);
                v.speed = std::min(v.speed, 1.0f);
            }
        }
    }

    Sim::CarStyle::Body TrafficSystem::PickBody(const float roll)
    {
        if (roll < 0.40f) return Sim::CarStyle::Body::Hatchback;
        if (roll < 0.62f) return Sim::CarStyle::Body::Sedan;
        if (roll < 0.78f) return Sim::CarStyle::Body::Estate;
        if (roll < 0.91f) return Sim::CarStyle::Body::Suv;
        return Sim::CarStyle::Body::Van;
    }

    int TrafficSystem::SpawnOn(const int lane, const float s, const float speed)
    {
        if (lane < 0 || static_cast<std::size_t>(lane) >= lanes_.Lanes().size()) {
            return -1;
        }
        TrafficVehicle v;
        v.id = nextId_++;
        v.lane = lane;
        v.link = -1;
        v.s = std::clamp(s, 0.0f, lanes_.LaneAt(lane).length);
        v.speed = std::max(0.0f, speed);
        std::uniform_real_distribution<float> factor(0.9f, 1.08f);
        // One draw, weighted towards the first four colours (white, silver, grey, black): they
        // take about two thirds of the cars, as they do on a real road.
        static constexpr int kPaletteDraw[24] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15};
        std::uniform_int_distribution<int> palette(0, 23);
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        std::uniform_int_distribution<unsigned> seed(0u, 6u);
        v.driverFactor = factor(rng_);
        v.paletteIndex = kPaletteDraw[palette(rng_)];
        v.body = PickBody(roll(rng_));
        v.styleSeed = seed(rng_);
        const Sim::CarStyle style = Sim::CarStyle::Preset(v.body, v.styleSeed);
        v.lengthM = style.length;
        v.widthM = style.width;
        v.heightM = style.height;
        v.massKg = Sim::TypicalMassKg(v.body);
        // Vans and SUVs drive a little more conservatively.
        if (v.body == Sim::CarStyle::Body::Van) v.driverFactor = std::min(v.driverFactor, 1.0f);
        v.plate = plates_.Next();
        ChooseNextLink(v);
        UpdatePose(v);
        vehicles_.push_back(v);
        ++spawnedTotal_;
        return v.id;
    }

    void TrafficSystem::Despawn(const PlayerProbe& player)
    {
        if (!player.valid) {
            return;
        }
        const float limit = world_.Data().traffic.despawnDistance;
        vehicles_.erase(std::remove_if(vehicles_.begin(), vehicles_.end(), [&](const TrafficVehicle& v) {
                            return Vector3::Distance(v.position, player.position) > limit && v.age > 5.0f;
                        }),
                        vehicles_.end());
    }

    void TrafficSystem::SpawnAroundPlayer(const PlayerProbe& player)
    {
        if (!player.valid || static_cast<int>(vehicles_.size()) >= maxVehicles_ || lanes_.Lanes().empty()) {
            return;
        }
        spawnTimer_ -= 1.0f / 60.0f;
        if (spawnTimer_ > 0.0f) {
            return;
        }
        spawnTimer_ = params.spawnInterval;
        const float minD = world_.Data().traffic.spawnMinDistance;
        const float maxD = world_.Data().traffic.despawnDistance * 0.85f;
        std::uniform_int_distribution<int> pickLane(0, static_cast<int>(lanes_.Lanes().size()) - 1);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const Vector2 playerXZ(player.position.X, player.position.Z);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const Lane& lane = lanes_.LaneAt(pickLane(rng_));
            if (lane.length < 20.0f) continue;
            const float s = 8.0f + unit(rng_) * (lane.length - 16.0f);
            const LanePoint p = lane.Evaluate(s);
            const float d = Vector2::Distance(Vector2(p.position.X, p.position.Z), playerXZ);
            if (d < minD || d > maxD) continue;
            // Not in front of the player within the view distance.
            Vector3 to = p.position - player.position;
            to.Y = 0.0f;
            if (to.LengthSquared() > 1e-4f) {
                to.Normalize();
                const float cosAngle = Vector3::Dot(to, player.forward);
                if (d < params.spawnViewDistance && cosAngle > std::cos(params.spawnViewAngleDeg * kPi / 180.0f)) continue;
            }
            if (LaneOccupiedNear(lane.id, s, 30.0f, -1)) continue;
            if (lane.oppositeLane >= 0) {
                // Avoid spawning nose to nose with someone on the opposite lane at the same spot.
                const Lane& opposite = lanes_.LaneAt(lane.oppositeLane);
                float lateral = 0.0f;
                const float os = opposite.Project(Vector2(p.position.X, p.position.Z), lateral);
                if (LaneOccupiedNear(opposite.id, os, 12.0f, -1)) continue;
            }
            const float limit = p.speedLimitKmh * kKmhToMs;
            SpawnOn(lane.id, s, limit * (0.6f + 0.3f * unit(rng_)));
            return;
        }
    }
}
