// Traffic population: deterministic body/plate selection, safe spawning and despawning.
// The original algorithms stay as TrafficSystem methods so the update path and tests retain
// their behaviour; this translation unit gives the population policy a clear home.
#include "CarSim/Traffic/TrafficSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace CarSim::Traffic
{
    using Map::Lane;
    using Map::LanePoint;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kKmhToMs = 1.0f / 3.6f;
    }

    Sim::CarStyle::Body TrafficSystem::PickBody(const float roll)
    {
        if (roll < 0.38f) return Sim::CarStyle::Body::Hatchback;
        if (roll < 0.58f) return Sim::CarStyle::Body::Sedan;
        if (roll < 0.73f) return Sim::CarStyle::Body::Estate;
        if (roll < 0.85f) return Sim::CarStyle::Body::Suv;
        if (roll < 0.93f) return Sim::CarStyle::Body::Van;
        if (roll < 0.97f) return Sim::CarStyle::Body::Truck;
        return Sim::CarStyle::Body::Bus;
    }

    int TrafficSystem::SpawnOn(const int lane, const float s, const float speed, const std::optional<Sim::CarStyle::Body> body)
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
        if (body) v.body = *body;
        v.styleSeed = seed(rng_);
        const Sim::CarStyle style = Sim::CarStyle::Preset(v.body, v.styleSeed);
        v.lengthM = style.length;
        v.widthM = style.width;
        v.heightM = style.height;
        v.massKg = Sim::TypicalMassKg(v.body);
        v.wheelbaseM = style.wheelbase;
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
            // Not in a bend, nor close to anybody: a bus appearing in a tight bend beside a car
            // coming the other way had nowhere to go.
            bool crowded = false;
            for (float d = -20.0f; d <= 20.0f && !crowded; d += 5.0f) {
                const float at = std::clamp(s + d, 0.0f, lane.length);
                crowded = std::fabs(lane.Evaluate(at).curvature) > 1.0f / 60.0f;
            }
            for (const auto& o : vehicles_) {
                if (crowded) break;
                crowded = Vector3::DistanceSquared(o.position, p.position) < 25.0f * 25.0f;
            }
            if (crowded) continue;
            const float speed = p.speedLimitKmh * kKmhToMs * (0.6f + 0.3f * unit(rng_));
            // Not within reach of the junction ahead: a car appearing there cannot stop for
            // traffic already on its way into the box, and drove into a car crossing it.
            if (lane.toIntersection >= 0 && lane.length - s < speed * speed / (2.0f * params.comfortDecel) + 30.0f) continue;
            SpawnOn(lane.id, s, speed);
            return;
        }
    }
}
