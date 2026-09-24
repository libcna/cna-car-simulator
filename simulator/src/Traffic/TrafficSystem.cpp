#include "CarSim/Traffic/TrafficSystem.hpp"

#include "CarSim/Collision/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
        constexpr float kKmhToMs = 1.0f / 3.6f;
        constexpr float kWheelRadius = 0.31f;

    }

    float TrafficSystem::LineSetback(const TrafficVehicle& v)
    {
        return (v.Heavy() ? 5.0f : 1.0f) + 0.5f * v.lengthM;
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
        // Bus stops: each shelter serves the lane it stands on the right of, a few metres off.
        busStops_.assign(lanes_.Lanes().size(), {});
        for (const auto& prop : world.Objects().Props()) {
            if (prop.type != Map::PropType::BusStop) continue;
            const Vector2 at(prop.position.X, prop.position.Z);
            int bestLane = -1;
            float bestS = 0.0f;
            float bestDistance = 9.0f;
            for (const auto& lane : lanes_.Lanes()) {
                float lateral = 0.0f;
                const float s = lane.Project(at, lateral);
                if (lateral < 0.5f || lateral > bestDistance) continue;
                if (s < 15.0f || s > lane.length - 15.0f) continue;   // not in a junction mouth
                bestDistance = lateral;
                bestLane = lane.id;
                bestS = s;
            }
            if (bestLane >= 0) busStops_[static_cast<std::size_t>(bestLane)].push_back(bestS);
        }
        for (auto& stops : busStops_) std::sort(stops.begin(), stops.end());
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

    void TrafficSystem::ChooseNextLink(TrafficVehicle& v)
    {
        v.nextLink = v.lane >= 0 ? lanes_.RandomLink(v.lane, rng_) : -1;
        // Buses and lorries keep to the through route where there is one: a twelve-metre body
        // does not turn inside a village junction without sweeping the next approach's stop
        // line. They still turn where the road only goes left or right.
        if (v.Heavy() && v.nextLink >= 0 && lanes_.LinkAt(v.nextLink).turn != Map::TurnType::Straight) {
            for (const int id : lanes_.LaneAt(v.lane).outgoingLinks) {
                if (lanes_.LinkAt(id).turn == Map::TurnType::Straight) {
                    v.nextLink = id;
                    break;
                }
            }
        }
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
        v.steerAngle = std::atan(v.wheelbaseM * p.curvature);
        if (v.Heavy()) {
            // A long rigid body is not the tangent at its middle: both axles run on the path and
            // the body lies along the chord between them. Posed on the tangent, a bus's ends
            // swung more than a metre out of its lane in a village bend.
            const float half = 0.5f * v.wheelbaseM;
            LanePoint front;
            LanePoint rear;
            bool haveRear = false;
            if (v.s - half >= 0.0f) {
                rear = v.link >= 0 ? lanes_.LinkAt(v.link).Evaluate(v.s - half) : lanes_.LaneAt(v.lane).Evaluate(v.s - half);
                haveRear = true;
            } else if (v.link >= 0) {
                const Lane& from = lanes_.LaneAt(lanes_.LinkAt(v.link).fromLane);
                if (from.length + v.s - half >= 0.0f) {
                    rear = from.Evaluate(from.length + v.s - half);
                    haveRear = true;
                }
            }
            if (haveRear && PathPointAhead(v, half, front)) {
                Vector3 chord = front.position - rear.position;
                chord.Y = 0.0f;
                if (chord.LengthSquared() > 1e-4f) {
                    v.position = (front.position + rear.position) * 0.5f;
                    chord.Normalize();
                    v.forward = chord;
                    v.headingRad = std::atan2(chord.X, -chord.Z);
                    v.steerAngle = std::atan(v.wheelbaseM * front.curvature);
                }
            }
        }
        if (v.lateral > 0.0f) {
            v.position += Vector3(v.forward.Z, 0.0f, -v.forward.X) * v.lateral;   // out to the left
        }
        // Keeping right: a bus or a lorry drives at the kerb edge of its lane, and a car meeting
        // one moves over too, as drivers do on a narrow village road. Without it the two bodies
        // touched across the centre line in the bends.
        if (v.link < 0 && v.lane >= 0) {
            const Lane& lane = lanes_.LaneAt(v.lane);
            bool moveOver = v.Heavy();
            if (!moveOver && lane.oppositeLane >= 0) {
                for (const auto& o : vehicles_) {
                    if (o.Heavy() && o.link < 0 && o.lane == lane.oppositeLane && Vector3::DistanceSquared(o.position, v.position) < 25.0f * 25.0f) {
                        moveOver = true;
                        break;
                    }
                }
            }
            const float spare = 0.5f * (lane.width - v.widthM) - 0.12f;
            if (moveOver && spare > 0.0f) {
                const Vector3 right(-v.forward.Z, 0.0f, v.forward.X);
                v.position += right * spare;
            }
        }
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
        // Buses and lorries: Czech limits for heavy vehicles (80 km/h out of town) and gentler
        // cornering.
        if (v.Heavy()) {
            desired = std::min(desired, 80.0f * kKmhToMs);
        }
        return desired;
    }

    TrafficSystem::Leader TrafficSystem::FindLeader(const TrafficVehicle& v, const PlayerProbe& player,
                                                    const PlayerProbe& pedestrian) const
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
            // The car we are overtaking stops being in our way once we are out beside it.
            if (o.id == v.overtaking && v.lateral > 1.2f && !v.returning) continue;
            for (const auto& seg : path) {
                const bool same = seg.isLink ? (o.link == seg.id) : (o.link < 0 && o.lane == seg.id);
                if (!same) continue;
                if (o.s < seg.sFrom - 0.5f) continue;
                const float ahead = seg.offset + (o.s - seg.sFrom);
                if (ahead > lookahead) continue;
                consider(ahead, o.lengthM, o.speed, o.id, false);
            }
        }
        // Somebody overtaking towards us in our lane: a head-on obstacle to stop for, well short
        // of it, so that it has the room to finish the pass or to drop back and pull in. And the
        // other way round: out on the wrong side ourselves, oncoming traffic is in our way.
        if (v.link < 0 && v.lane >= 0 && lanes_.LaneAt(v.lane).oppositeLane >= 0) {
            const int opposite = lanes_.LaneAt(v.lane).oppositeLane;
            for (const auto& o : vehicles_) {
                if (o.id == v.id || o.link >= 0 || o.lane != opposite) continue;
                const bool headOn = o.lateral >= 0.8f || v.lateral >= 0.8f;
                if (!headOn) continue;
                const float ahead = OppositeS(o, v.lane) - v.s;
                if (ahead > 0.0f && ahead <= lookahead) consider(ahead - (o.lateral >= 0.8f ? 10.0f : 0.0f), o.lengthM, 0.0f, o.id, false);
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
        if (player.valid && player.blocksTraffic && playerLane_ >= 0) {
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
        // A bus or a lorry coming the other way round a tight village bend, or turning across
        // us, can put its body into our lane. A car waits short of where the body is going to
        // swing, as a driver would. A car already standing in that swing drives on out of it,
        // minding only where the body is now -- stopping there would leave it in the way -- and
        // the bus waits for it before the bend (below). Two buses or lorries meeting at a bend
        // settle it the same way, and where neither is in the other's swing yet (or both are),
        // the one with the lower id has the bend: every pair has exactly one that goes.
        for (const auto& o : vehicles_) {
            if (o.id == v.id || !o.Heavy() || (o.link < 0 && o.lane == v.lane)) continue;
            if (Vector3::DistanceSquared(o.position, v.position) > 60.0f * 60.0f) continue;
            // Following us onto our lane (not turning round to meet us): it treats us as its
            // leader; its way is not in ours.
            const int itsLink = o.link >= 0 ? o.link : o.nextLink;
            if (v.link < 0 && itsLink >= 0 && lanes_.LinkAt(itsLink).toLane == v.lane && lanes_.LinkAt(itsLink).turn != Map::TurnType::UTurn) continue;
            // Far enough ahead to stop without braking hard.
            const float reach = std::max(30.0f, v.speed * v.speed / (2.0f * params.comfortDecel) + 10.0f);
            float blockedAt = -1.0f;
            if (v.Heavy()) {
                // Only a bus or a lorry meeting us on the other lane, when it goes first, or one
                // going through the junction ahead; elsewhere the lane and junction rules keep two
                // of them apart.
                const bool meeting = v.link < 0 && v.lane >= 0 && o.link < 0 && o.lane == lanes_.LaneAt(v.lane).oppositeLane;
                // One inside a junction, or on its way through it, swings its body over the ends of
                // the approaches: a bus coming up to one of them stops short of that, as a car
                // does, instead of pulling up at its line right in the way.
                bool goingThrough = v.link < 0 && GoingThrough(o);
                // Both on their way into the same junction: one of the two goes first, the one
                // committed to it, else the lower id; each waiting for the other's swing stood
                // them both at the mouth for good.
                if (goingThrough && o.link < 0 && (v.committed || v.claimed) && o.nextLink >= 0 && v.nextLink >= 0 &&
                    lanes_.LinkAt(o.nextLink).intersection == lanes_.LinkAt(v.nextLink).intersection) {
                    const bool weGo = v.committed != o.committed ? v.committed : v.id < o.id;
                    if (weGo) goingThrough = false;
                }
                if (!(meeting && !HeavyGoesFirst(v, o)) && !goingThrough) continue;
                // Body against body, with no margin: two of them on a narrow village road pass
                // with a hand's breadth to spare, which a path check a little wider than the body
                // never allows; and a swing through a junction comes over the side of the
                // approach, beside the path rather than on it.
                const float d = BodiesMeetAhead(v, o, reach);
                if (d >= 0.0f) consider(d + 0.5f * v.lengthM, 0.0f, 0.0f, o.id, true);
                continue;
            } else {
                // Inside the junction a car clears it, minding only where the body is now.
                blockedAt = v.link >= 0 || HeavySwingMeets(o, v) >= 0.0f ? PathBlockedBy(v, o, reach) : SweptPathBlockedBy(v, o, reach);
            }
            if (blockedAt < 0.0f) continue;
            consider(blockedAt + 0.5f * o.lengthM, o.lengthM, std::max(0.0f, Vector3::Dot(o.Velocity(), v.forward)), o.id, true);
        }
        // The bus's side of it: a car coming the other way that stands where the bus's body will
        // swing gets the bend first (two buses or lorries are settled above). It gets the rest of its way round, not only the spot it stands on: the bus holds back
        // where its body would first reach that way. Stopping only short of where the car stood,
        // the bus crept on as the car came round, and the two met nose to nose in the bend.
        if (v.Heavy()) {
            for (const auto& o : vehicles_) {
                if (o.id == v.id) continue;
                if (Vector3::DistanceSquared(o.position, v.position) > 60.0f * 60.0f) continue;
                if (o.Heavy() || HeavySwingMeets(v, o) < 0.0f) continue;
                float reaches = 0.0f;
                const float horizon = SwingHorizon(v);
                for (float f = 2.0f; f <= horizon; f += 2.0f) {
                    Vector3 centre;
                    float heading = 0.0f;
                    if (!HeavyPoseAhead(v, f, centre, heading)) break;
                    if (PathBlockedByBody(o, v, centre, heading, 30.0f) >= 0.0f) {
                        reaches = f;
                        break;
                    }
                }
                // Our centre can still move up to about two metres short of that pose.
                consider(std::max(0.0f, reaches - 2.0f) + 0.5f * v.lengthM, 0.0f, 0.0f, o.id, true);
            }
        }
        // Also treat the player as an obstacle when it physically sits on our path (any heading).
        if (player.valid && player.blocksTraffic) {
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
        const auto stopForPerson = [&](const PlayerProbe& person) {
            if (!person.valid || Vector3::DistanceSquared(v.position, person.position) > lookahead * lookahead) return;
            // Sample the road ahead, including connectors through intersections. A person
            // anywhere across the car's swept width is a stationary obstacle until clear.
            const float clearance = 0.5f * v.widthM + 0.55f;
            for (float ahead = 0.0f; ahead <= lookahead; ahead += 0.5f) {
                LanePoint point;
                if (!PathPointAhead(v, ahead, point)) break;
                const float dx = point.position.X - person.position.X;
                const float dz = point.position.Z - person.position.Z;
                if (dx * dx + dz * dz > clearance * clearance ||
                    std::fabs(point.position.Y - person.position.Y) > 2.5f) continue;
                consider(ahead, person.lengthM, 0.0f, -2, false);
                break;
            }
        };
        stopForPerson(pedestrian);
        for (const auto& person : pedestrians_) stopForPerson(person);
        return best;
    }

    void TrafficSystem::UpdateVehicle(TrafficVehicle& v, const float dt, const PlayerProbe& player,
                                      const PlayerProbe& pedestrian)
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
        // Nobody ahead of us on our lane: the first in the queue at the junction. Commitment on
        // green is for that one only; one committed while still behind another crept after it
        // and crossed on the red that came meanwhile.
        const auto headOfLane = [&]() {
            for (const auto& o : vehicles_) {
                if (o.id != v.id && o.link < 0 && o.lane == v.lane && o.s > v.s) return false;
            }
            return true;
        };
        float desired = DesiredSpeedAhead(v);
        Leader leader = FindLeader(v, player, pedestrian);
        UpdateOvertake(v, leader, dt, desired, player);
        if (v.overtaking >= 0 && v.lateral > 1.2f && !v.returning && leader.found && leader.id == v.overtaking) {
            leader = FindLeader(v, player, pedestrian);   // now beside it: look past it
        }
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
                // Queued ahead on our connector, or standing at the start of the lane it leads
                // onto: creeping out of the box would only drive into the back of it.
                const LaneLink& ours = lanes_.LinkAt(v.link);
                for (const auto& o : vehicles_) {
                    if (o.id == v.id) continue;
                    if (o.link == v.link && o.s > v.s && o.s - v.s < v.lengthM + 6.0f) queuedAhead = true;
                    if (o.link < 0 && o.lane == ours.toLane && ours.length - v.s + o.s < 0.5f * (v.lengthM + o.lengthM) + 6.0f) queuedAhead = true;
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
            // Commitment survives the change of a light, but not a stop. A car that committed on
            // green and then had to queue behind another kept its commitment while it stood
            // there, and drove into the junction on red when the queue moved.
            if (signalised && v.committed && v.speed < 0.4f && distanceToEnd > LineSetback(v)) {
                v.committed = false;
            }
            // A permissive turn that has been waiting at the line on green for a gap in the
            // oncoming stream clears the junction when the light changes, once the oncoming
            // traffic has stopped, as drivers do; otherwise it would wait through every cycle.
            const bool clearingTurn = signalised && v.yieldingOnGreen && !link.yieldTo.empty() &&
                                      distanceToEnd - LineSetback(v) < 1.5f;
            if (signalised && distanceToEnd < 60.0f && !v.committed && !clearingTurn) {
                // Amber means stop unless that would mean braking harder than a normal stop, in
                // which case the car is already too close and carries on.
                const float comfortableStop = v.speed * v.speed / (2.0f * 3.0f) + 1.0f;
                const bool mustStop = aspect == SignalAspect::Red || aspect == SignalAspect::RedAmber ||
                                      (aspect == SignalAspect::Amber && distanceToEnd > comfortableStop);
                const float lineGap = distanceToEnd - LineSetback(v);
                // Over the line itself (not merely inside the room a bus or a lorry leaves).
                const bool overTheLine = distanceToEnd - 1.0f - 0.5f * v.lengthM < 0.0f;
                if (mustStop && overTheLine) {
                    // Already over the line when it changed. Holding here parked the car in the
                    // mouth of the junction and then crept it across at walking pace, which is
                    // both wrong and the thing that blocks a box. A driver in this position
                    // clears the junction, so the car commits and goes.
                    v.committed = true;
                } else if (mustStop) {
                    signalHold = true;
                    v.waiting = true;
                    v.waitTime += dt;
                    v.stoppedAtLine = false;
                    if (lineGap < gap) {
                        gap = lineGap;
                        leaderSpeed = 0.0f;
                    }
                } else if (distanceToEnd < 12.0f && headOfLane() && (link.yieldTo.empty() || MayEnterIntersection(v, player)) &&
                           !HeavySweepHitsStanding(v)) {
                    // Through on green: do not stop halfway on a change. A turn that gives way
                    // on green (left across the oncoming stream) commits only once it may go.
                    v.committed = true;
                }
            }
            // On green a permissive turn still gives way to the conflicting green movements the
            // lane graph lists for it; before this, a left turn on green crossed the oncoming
            // stream blind.
            const bool controlled = (!signalised &&
                                     (link.control == Map::ApproachControl::Yield || link.control == Map::ApproachControl::Stop ||
                                      link.control == Map::ApproachControl::RightHandRule || !link.yieldTo.empty())) ||
                                    (signalised && !signalHold && !link.yieldTo.empty());
            if (!(controlled && signalised)) v.yieldingOnGreen = false;
            if (controlled && distanceToEnd < 40.0f) {
                bool hold = false;
                if (link.control == Map::ApproachControl::Stop && !v.stoppedAtLine) {
                    // Come to a full stop at the line first.
                    hold = true;
                    // Measured to the centre, so a long vehicle's nose is already at the line
                    // further out.
                    if (distanceToEnd < 2.5f + std::max(0.0f, 0.5f * v.lengthM - 2.3f) + (v.Heavy() ? 4.0f : 0.0f) && v.speed < 0.3f) {
                        v.stoppedAtLine = true;
                    }
                }
                if (!hold && !v.committed && !MayEnterIntersection(v, player)) {
                    hold = true;
                }
                if (signalised) v.yieldingOnGreen = hold;
                if (hold) {
                    v.waiting = true;
                    v.waitTime += dt;
                    // Deadlock breaker: after a long wait with nothing moving inside the junction,
                    // the car that has waited longest commits and goes; the others keep waiting
                    // until it is through (the released car stays committed until it is on the link).
                    // Only for the one standing at the line: released while still queued behind
                    // another, a car crept up to the junction over several seconds, too late for
                    // the traffic that had meanwhile come too close to stop for it.
                    const bool atTheLine = distanceToEnd - LineSetback(v) < 3.0f;
                    if (v.waitTime > params.deadlockSeconds && atTheLine) {
                        bool anyoneInside = false;
                        bool someoneElseFirst = false;
                        for (const auto& o : vehicles_) {
                            if (o.id == v.id) continue;
                            if (o.link >= 0 && lanes_.LinkAt(o.link).intersection == link.intersection &&
                                (o.speed > 0.3f || std::find(link.conflicts.begin(), link.conflicts.end(), o.link) != link.conflicts.end())) {
                                anyoneInside = true;
                            }
                            // A bus or a lorry standing over its line has its nose in the junction.
                            if (o.Heavy() && o.link < 0 && o.nextLink >= 0 && lanes_.LinkAt(o.nextLink).intersection == link.intersection &&
                                DistanceToEnd(o) < 1.0f + 0.5f * o.lengthM) {
                                anyoneInside = true;
                            }
                            if (o.link < 0 && o.nextLink >= 0 && lanes_.LinkAt(o.nextLink).intersection == link.intersection) {
                                // A committed car only goes first if it is at the front of its lane;
                                // one left committed in a queue behind another would hold the whole
                                // junction for ever.
                                bool frontOfLane = true;
                                for (const auto& q : vehicles_) {
                                    if (q.id != o.id && q.link < 0 && q.lane == o.lane && q.s > o.s) {
                                        frontOfLane = false;
                                        break;
                                    }
                                }
                                // A bus or a lorry that cannot go for somebody standing in its way
                                // (us, as often as not) is not first, however long it has waited.
                                const bool cannotGo = o.Heavy() && HeavySweepHitsStanding(o);
                                if ((o.committed && frontOfLane) || (!cannotGo && (o.waitTime > v.waitTime || (o.waitTime == v.waitTime && o.id < v.id)))) {
                                    someoneElseFirst = true;
                                }
                            }
                            // Traffic arriving too fast to stop for us goes first: releasing into
                            // its path is what put two cars nose to flank in the box.
                            // A bus or a lorry needs the junction to itself (BoxClearFor), and its body
                            // sweeps outside the car-sized conflict map: for one, anybody heading into
                            // this junction counts, and so does anybody who has claimed it.
                            const bool heavyPair = (v.Heavy() || o.Heavy()) && o.link < 0 && o.nextLink >= 0 &&
                                                   lanes_.LinkAt(o.nextLink).intersection == link.intersection;
                            if (heavyPair && o.claimed) someoneElseFirst = true;
                            if (v.Heavy() && HeavySweepHitsStanding(v)) someoneElseFirst = true;
                            if (o.link < 0 && o.nextLink >= 0 && o.speed > 2.0f &&
                                (heavyPair || std::find(link.conflicts.begin(), link.conflicts.end(), o.nextLink) != link.conflicts.end())) {
                                const float toLine = lanes_.LaneAt(o.lane).length - o.s;
                                // Only traffic that can no longer stop: anyone else sees the released
                                // car claim the junction and waits for it (BoxClearFor).
                                if (toLine < o.speed * o.speed / (2.0f * params.comfortDecel) + 8.0f) {
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
                    const float lineGap = distanceToEnd - LineSetback(v);
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
                // Priority movements too: nobody drives into a junction another car is still
                // crossing, or while a bus or a lorry is inside. Only while it can still stop at
                // the line; past that point it is committed like any driver would be.
                const float lineGap = distanceToEnd - LineSetback(v);
                const float stopping = v.speed * v.speed / (2.0f * params.comfortDecel);
                // Looked at from far enough out to stop: from a fixed 60 m a car doing 60 km/h had
                // no stopping distance left by the time it looked, and never did.
                if (!v.committed && lineGap > 0.0f && lineGap > stopping - 1.0f && distanceToEnd < std::max(60.0f, stopping + 20.0f) && !BoxClearFor(v)) {
                    if (lineGap < gap) {
                        gap = std::max(0.05f, lineGap);
                        leaderSpeed = 0.0f;
                    }
                }
            }
        }
        // Time spent at the line, creeping included, until into the junction.
        const bool atLine = v.link < 0 && v.nextLink >= 0 && distanceToEnd < LineSetback(v) + 10.0f;
        v.standingAtLine = atLine ? v.standingAtLine + dt : 0.0f;
        // A car that will not stop at the line this frame and is inside its stopping distance
        // has claimed its way through the junction: BoxClearFor treats it as already inside, so
        // two cars cannot both decide on the same empty box.
        v.claimed = false;
        if (v.link < 0 && v.nextLink >= 0) {
            const float lineGap = distanceToEnd - LineSetback(v);
            // Waiting to give way, or held at the line this frame (a car stopped a little over the
            // line has its hold clamped to a tiny positive gap).
            const bool heldAtLine = v.waiting || gap <= std::max(lineGap, 0.05f) + 0.1f;
            // Creeping off the line counts too: it is about to be in the box.
            v.claimed = !heldAtLine && lineGap < std::max(3.0f, v.speed * v.speed / (2.0f * params.comfortDecel) + 2.0f);
            // Told to give way too late to stop even hard: it is going in regardless, so it had
            // better be seen as going in.
            if (v.speed > 1.0f && lineGap < v.speed * v.speed / (2.0f * 6.0f) + 0.5f) v.claimed = true;
            // A bus or a lorry needs the junction to itself, and on a busy road somebody always
            // has a claim on it: one waited there cycle after cycle. After a long wait at the
            // line (the time there, not the current wait, which starts again with every green and
            // every moment the way looked clear), when its own light is not red, its exit is free
            // and its way through touches nobody standing, it claims the junction: the others
            // hold back until it is through.
            const bool longWait = v.standingAtLine > 2.0f * params.deadlockSeconds;
            if (v.Heavy() && longWait && ExitFreeFor(v) && !HeavySweepHitsStanding(v)) {
                const LaneLink& ours = lanes_.LinkAt(v.nextLink);
                const bool red = ours.control == Map::ApproachControl::Signal && ours.signalGroup >= 0 &&
                                 AspectOf(ours.intersection, ours.signalGroup) != SignalAspect::Green;
                // One at a time, and only the one at the head of its lane: two of them claiming
                // the same junction held each other off, and one queued behind another held the
                // whole junction for nothing.
                bool another = false;
                for (const auto& o : vehicles_) {
                    if (o.id == v.id) continue;
                    if ((o.Heavy() && o.claimed && o.link < 0 && o.nextLink >= 0 &&
                         lanes_.LinkAt(o.nextLink).intersection == ours.intersection) ||
                        (o.link < 0 && o.lane == v.lane && o.s > v.s) || o.link == v.nextLink) {
                        another = true;
                        break;
                    }
                }
                if (!red && !another) v.claimed = true;
            }
        }
        // Buses call at the stops on their lane: indicate right, pull in, stand a while with
        // the doors open, indicate left and go.
        if (v.body == Sim::CarStyle::Body::Bus && v.link < 0 && v.lane >= 0 && static_cast<std::size_t>(v.lane) < busStops_.size()) {
            if (v.dwell > 0.0f) {
                v.dwell -= dt;
                desired = 0.0f;
                gap = std::min(gap, 0.05f);
                leaderSpeed = 0.0f;
                v.indicatorLeft = v.dwell < 3.0f;
                v.indicatorRight = false;
            } else {
                for (const float stop : busStops_[static_cast<std::size_t>(v.lane)]) {
                    if (v.servedLane == v.lane && std::fabs(v.servedS - stop) < 0.5f) continue;
                    const float ahead = stop - v.s;
                    if (ahead < -2.0f || ahead > 70.0f) continue;
                    v.indicatorRight = true;
                    v.indicatorLeft = false;
                    // As an obstacle just past the stop, so the car-following gap brings the
                    // bus to rest with its middle at the shelter.
                    const float stopGap = ahead + params.minGap + 1.0f;
                    if (stopGap < gap) {
                        gap = std::max(0.05f, stopGap);
                        leaderSpeed = 0.0f;
                    }
                    if (ahead < 1.5f && v.speed < 0.4f) {
                        std::uniform_real_distribution<float> dwell(12.0f, 20.0f);
                        v.dwell = dwell(rng_);
                        v.servedLane = v.lane;
                        v.servedS = stop;
                    }
                    break;
                }
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
        // A lorry or a bus pulls away at about 60 % of a car's rate and keeps longer gaps.
        auto vehicleParams = params;
        if (v.Heavy()) {
            vehicleParams.maxAccel *= 0.6f;
            vehicleParams.timeHeadway *= 1.3f;
            vehicleParams.minGap += 1.0f;
        }
        float accel = IdmAcceleration(v.speed, desired, gap, leaderSpeed, vehicleParams);
        accel = std::clamp(accel, -8.0f, vehicleParams.maxAccel);
        float newSpeed = std::max(0.0f, v.speed + accel * dt);
        float distance = 0.5f * (v.speed + newSpeed) * dt;
        if (leader.found && leader.id == -2) {
            // If someone steps into the lane inside the normal braking distance, stop at
            // their space instead of letting the kinematic car drive through them.
            const float remaining = std::max(0.0f, leader.gap - 0.2f);
            if (distance > remaining) {
                distance = remaining;
                newSpeed = 0.0f;
                accel = -v.speed / dt;
            }
        }
        v.acceleration = accel;
        v.brakeLights = accel < -0.6f || (v.stunned > 0.0f && v.speed > 0.1f);
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
                // A release to clear the box ends with the box: carried onto the next lane, the
                // commitment let the car into the next junction without giving way at all.
                v.committed = false;
                v.clearingBox = false;
                v.yieldingOnGreen = false;
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

    void TrafficSystem::Update(const float dt, const PlayerProbe& player, const PlayerProbe& pedestrian)
    {
        if (dt <= 0.0f) {
            return;
        }
        // Project the player onto the lane graph once per update.
        playerLane_ = -1;
        if (player.valid && player.blocksTraffic) {
            const float heading = std::atan2(player.forward.X, -player.forward.Z);
            playerLane_ = lanes_.NearestLane(Vector2(player.position.X, player.position.Z), heading, 4.0f, &playerS_);
        }
        signals_.Update(dt);
        const PlayerProbe& focus = pedestrian.valid ? pedestrian : player;
        Despawn(focus);
        SpawnAroundPlayer(focus);
        for (auto& v : vehicles_) {
            UpdateVehicle(v, dt, player, pedestrian);
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

}
