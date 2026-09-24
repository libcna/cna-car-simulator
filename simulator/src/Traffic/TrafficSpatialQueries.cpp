// Path and body-footprint queries used by traffic following and junction safety.
// The established geometry algorithms are retained unchanged.
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
        return PathBlockedByBody(v, other, other.position, other.headingRad, maxAhead);
    }

    float TrafficSystem::PathBlockedByBody(const TrafficVehicle& v, const TrafficVehicle& other, const Vector3& centre, const float headingRad,
                                           const float maxAhead) const
    {
        const Collision::Obb box = Collision::Obb::FromHeading(centre + Vector3(0.0f, 0.5f * other.heightM, 0.0f),
                                                               Vector3(0.5f * other.widthM, 0.5f * other.heightM, 0.5f * other.lengthM),
                                                               headingRad);
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

    float TrafficSystem::SweptPathBlockedBy(const TrafficVehicle& v, const TrafficVehicle& heavy, const float maxAhead) const
    {
        // Round a tight bend a bus's body sweeps across the centre line before it gets there. A
        // car that waited only for where the bus was stopped too late, in the swing of its body,
        // and one that waited for the next few seconds of it stopped where the swing came later.
        // So: the whole of the bus's way along its lane, posed as UpdatePose poses it, along the
        // chord between the axles.
        float best = PathBlockedBy(v, heavy, maxAhead);
        if (heavy.link < 0 && heavy.lane < 0) return best;
        const float horizon = SwingHorizon(heavy);
        for (float f = 2.0f; f <= horizon; f += 2.0f) {
            Vector3 centre;
            float heading = 0.0f;
            if (!HeavyPoseAhead(heavy, f, centre, heading)) break;
            const float d = PathBlockedByBody(v, heavy, centre, heading, maxAhead);
            if (d >= 0.0f && (best < 0.0f || d < best)) best = d;
        }
        return best;
    }

    float TrafficSystem::SwingHorizon(const TrafficVehicle& heavy) const
    {
        // Along its lane, and no further than its stop line where the lane ends at a junction --
        // a car waiting in the box for a bus that was itself waiting at the line for the box
        // locked the two for good -- unless it is going through: then all the way through and
        // out, which is where its body swings over the end of the next approach. A car that
        // came up to its line there after the bus had gone for the junction (or crept over its
        // own line) stopped right in it.
        const float half = 0.5f * heavy.lengthM;
        if (heavy.link >= 0) {
            return std::min(40.0f, lanes_.LinkAt(heavy.link).length - heavy.s + half);
        }
        const Lane& lane = lanes_.LaneAt(heavy.lane);
        if (lane.toIntersection < 0) return std::min(40.0f, lane.length - heavy.s - 0.5f * heavy.wheelbaseM);
        if (GoingThrough(heavy)) {
            return std::min(60.0f, lane.length - heavy.s + lanes_.LinkAt(heavy.nextLink).length + half);
        }
        return std::min(40.0f, lane.length - heavy.s - LineSetback(heavy));
    }

    bool TrafficSystem::GoingThrough(const TrafficVehicle& heavy) const
    {
        // Inside the junction, committed to it, on the move with a claim on it, or over its line.
        // A claim alone is not going: a bus standing on one after a long wait is still waiting
        // for the box to empty, and the cars in the box giving way to its way through never did.
        if (heavy.link >= 0) return true;
        if (heavy.lane < 0 || heavy.nextLink < 0) return false;
        const bool overTheLine = DistanceToEnd(heavy) < 1.0f + 0.5f * heavy.lengthM;
        return heavy.committed || (heavy.claimed && heavy.speed > 0.5f) || overTheLine;
    }

    float TrafficSystem::HeavySwingMeets(const TrafficVehicle& heavy, const TrafficVehicle& car) const
    {
        if (heavy.link >= 0 || heavy.lane < 0 || car.link >= 0 || car.lane != lanes_.LaneAt(heavy.lane).oppositeLane) return -1.0f;
        // The car a little larger than it is and the swing finely sampled: on the edge of it the
        // answer flickered as the bus crept on, and each moment it said no the bus crept further
        // into the bend, until it stood in the car's way.
        const Collision::Obb theirs = Collision::Obb::FromHeading(car.position + Vector3(0.0f, 0.5f * car.heightM, 0.0f),
                                                                  Vector3(0.5f * car.widthM + 0.25f, 0.5f * car.heightM, 0.5f * car.lengthM + 0.25f),
                                                                  car.headingRad);
        const float horizon = SwingHorizon(heavy);
        for (float f = 1.0f; f <= horizon; f += 1.0f) {
            Vector3 centre;
            float heading = 0.0f;
            if (!HeavyPoseAhead(heavy, f, centre, heading)) break;
            const Collision::Obb ours = Collision::Obb::FromHeading(centre + Vector3(0.0f, 0.5f * heavy.heightM, 0.0f),
                                                                    Vector3(0.5f * heavy.widthM, 0.5f * heavy.heightM, 0.5f * heavy.lengthM), heading);
            Collision::Contact contact;
            if (Collision::IntersectObbObb(ours, theirs, contact)) return f;
        }
        return -1.0f;
    }

    float TrafficSystem::BodiesMeetAhead(const TrafficVehicle& v, const TrafficVehicle& heavy, const float reach) const
    {
        // How far v's centre can go along its path before its body, posed as it will be, touches
        // anywhere the heavy vehicle's body is going to be; -1 when never within reach.
        std::vector<Collision::Obb> sweep;
        const float horizon = SwingHorizon(heavy);
        for (float f = 0.0f; f <= horizon; f += 2.0f) {
            Vector3 centre;
            float heading = 0.0f;
            if (!HeavyPoseAhead(heavy, f, centre, heading)) break;
            sweep.push_back(Collision::Obb::FromHeading(centre + Vector3(0.0f, 0.5f * heavy.heightM, 0.0f),
                                                        Vector3(0.5f * heavy.widthM, 0.5f * heavy.heightM, 0.5f * heavy.lengthM), heading));
        }
        for (float d = 0.0f; d <= reach; d += 1.5f) {
            Vector3 centre;
            float heading = 0.0f;
            if (!HeavyPoseAhead(v, d, centre, heading)) break;
            const Collision::Obb ours = Collision::Obb::FromHeading(centre + Vector3(0.0f, 0.5f * v.heightM, 0.0f),
                                                                    Vector3(0.5f * v.widthM, 0.5f * v.heightM, 0.5f * v.lengthM), heading);
            for (const auto& theirs : sweep) {
                Collision::Contact contact;
                if (Collision::IntersectObbObb(ours, theirs, contact)) return std::max(0.0f, d - 2.0f);
            }
        }
        return -1.0f;
    }

    bool TrafficSystem::HeavyPoseAhead(const TrafficVehicle& heavy, const float ahead, Vector3& centre, float& headingRad) const
    {
        const float half = 0.5f * heavy.wheelbaseM;
        LanePoint front;
        LanePoint rear;
        if (!PathPointAhead(heavy, ahead + half, front) || !PathPointAhead(heavy, ahead - half, rear)) return false;
        Vector3 chord = front.position - rear.position;
        chord.Y = 0.0f;
        if (chord.LengthSquared() < 1e-4f) return false;
        centre = (front.position + rear.position) * 0.5f;
        headingRad = std::atan2(chord.X, -chord.Z);
        // At the kerb edge of its lane, as UpdatePose keeps it. Posed on the centre line, two
        // buses passing on a narrow road each found the other's way blocked and both stopped.
        if (heavy.link < 0 && heavy.lane >= 0) {
            const float spare = 0.5f * (lanes_.LaneAt(heavy.lane).width - heavy.widthM) - 0.12f;
            chord.Normalize();
            if (spare > 0.0f) centre += Vector3(-chord.Z, 0.0f, chord.X) * spare;
        }
        return true;
    }

    bool TrafficSystem::HeavyGoesFirst(const TrafficVehicle& a, const TrafficVehicle& b) const
    {
        // The one already standing in the other's swing has the bend -- the other one can still
        // stop short of it. Neither yet, or both: the lower id.
        const bool aInItsSwing = HeavySwingMeets(b, a) >= 0.0f;
        const bool bInOurSwing = HeavySwingMeets(a, b) >= 0.0f;
        if (aInItsSwing != bInOurSwing) return aInItsSwing;
        return a.id < b.id;
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

}
