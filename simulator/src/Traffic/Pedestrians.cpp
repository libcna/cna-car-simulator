#include "CarSim/Traffic/Pedestrians.hpp"

#include <algorithm>
#include <cmath>

namespace CarSim::Traffic
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    Pedestrians::Pedestrians(const Map::MapWorld& world, const std::uint32_t seed)
        : world_(world), rng_(seed)
    {
        const auto& network = world.Roads();
        // Pavements: the urban stretches of roads with a sidewalk, one walkway per side, kept a
        // dozen metres clear of every junction.
        for (const auto& road : network.Roads()) {
            if (road.profile.sidewalk.width <= 0.0f) continue;
            const auto& samples = road.curve.Samples();
            for (const bool right : {true, false}) {
                if (road.profile.SidewalkOuter(right) <= 0.0f) continue;
                const float lateral = (right ? 1.0f : -1.0f) * (road.profile.HalfPavedWidth() + 0.5f * road.profile.sidewalk.width);
                for (const int pieceIndex : road.pieces) {
                    const auto& piece = network.Pieces()[static_cast<std::size_t>(pieceIndex)];
                    float runStart = -1.0f;
                    const auto flush = [&](const float end) {
                        if (runStart >= 0.0f && end - runStart > 20.0f) walkways_.push_back({road.index, runStart, end, lateral});
                        runStart = -1.0f;
                    };
                    for (const auto& sample : samples) {
                        if (sample.s < piece.s0 + 12.0f || sample.s > piece.s1 - 12.0f) continue;
                        if (sample.urban) {
                            if (runStart < 0.0f) runStart = sample.s;
                        } else {
                            flush(sample.s);
                        }
                    }
                    flush(piece.s1 - 12.0f);
                }
            }
        }
        // Zebra crossings: wherever an IP 6 sign stands by a road.
        for (const auto& sign : world.Objects().Signs()) {
            if (!sign.spec || sign.spec->code != "IP6") continue;
            Map::RoadHit hit;
            if (!network.NearestRoad(Vector2(sign.position.X, sign.position.Z), 14.0f, hit)) continue;
            const auto& road = network.Roads()[static_cast<std::size_t>(hit.road)];
            bool duplicate = false;
            for (const auto& c : crossings_) duplicate = duplicate || (c.road == hit.road && std::fabs(c.s - hit.s) < 6.0f);
            if (!duplicate) crossings_.push_back({hit.road, hit.s, road.profile.HalfPavedWidth()});
        }
    }

    int Pedestrians::Spawn(const int walkway, const float s, const float direction, const bool willCross)
    {
        if (walkway < 0 || static_cast<std::size_t>(walkway) >= walkways_.size()) return -1;
        const Walkway& w = walkways_[static_cast<std::size_t>(walkway)];
        Pedestrian p;
        p.id = nextId_++;
        p.walkway = walkway;
        p.s = std::clamp(s, w.s0, w.s1);
        p.direction = direction >= 0.0f ? 1.0f : -1.0f;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        p.speed = 1.1f + 0.45f * unit(rng_);
        p.lateral = w.lateral + (unit(rng_) - 0.5f) * 0.6f;
        p.look = static_cast<unsigned>(rng_());
        p.phase = unit(rng_) * 6.2831853f;
        p.nextCrossChance = willCross ? 0.0f : unit(rng_);
        UpdatePose(p);
        people_.push_back(p);
        return p.id;
    }

    void Pedestrians::UpdatePose(Pedestrian& p) const
    {
        const Walkway& w = walkways_[static_cast<std::size_t>(p.walkway)];
        const auto& road = world_.Roads().Roads()[static_cast<std::size_t>(w.road)];
        const Map::RoadSample sample = road.curve.Evaluate(p.s);
        Vector3 t(sample.tangent.X, 0.0f, sample.tangent.Z);
        if (t.LengthSquared() < 1e-8f) t = Vector3(0.0f, 0.0f, -1.0f);
        t.Normalize();
        const Vector3 right(-t.Z, 0.0f, t.X);
        const Vector3 flat = sample.position + right * p.lateral;
        p.position = Vector3(flat.X, world_.Ground().HeightAt(flat.X, flat.Z), flat.Z);
        Vector3 facing = t * p.direction;
        if (p.crossing != -1) facing = right * (p.crossFrom > 0.0f ? -1.0f : 1.0f);
        p.headingRad = std::atan2(-facing.X, -facing.Z);
        p.onRoad = std::fabs(p.lateral) < road.profile.HalfPavedWidth() + 0.3f;
    }

    bool Pedestrians::GapToCross(const Pedestrian& p, const std::vector<TrafficVehicle>& vehicles, const PlayerProbe& player) const
    {
        // Nothing moving within the distance it would cover while we cross, plus a margin; a
        // car standing still (it stopped for us) is fine.
        // Away from a zebra, people wait for a much bigger gap.
        const float margin = p.crossing >= 0 ? 12.0f : 30.0f;
        const float crossSeconds = 2.0f * p.crossHalf / p.speed + 1.5f;
        const auto threatens = [&](const Vector3& at, const float speed) {
            if (speed < 0.5f) return false;
            return Vector3::Distance(at, p.position) < margin + speed * crossSeconds;
        };
        for (const auto& v : vehicles) {
            if (threatens(v.position, v.speed)) return false;
        }
        if (player.valid && threatens(player.position, std::fabs(player.speed))) return false;
        return true;
    }

    void Pedestrians::Update(const float dt, const Vector3& focus, const std::vector<TrafficVehicle>& vehicles, const PlayerProbe& player,
                             const int count)
    {
        if (walkways_.empty() || dt <= 0.0f) return;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        // Keep a crowd near the focus: drop the far ones, place new ones 60-260 m out.
        people_.erase(std::remove_if(people_.begin(), people_.end(),
                                     [&](const Pedestrian& p) { return p.crossing == -1 && Vector3::Distance(p.position, focus) > 320.0f; }),
                      people_.end());
        // The first fill (the world has just appeared) may place people anywhere; later ones
        // appear out of the driver's immediate surroundings.
        const bool firstFill = people_.empty() && !filled_;
        filled_ = true;
        for (int attempt = 0; attempt < (firstFill ? 4000 : 40) && static_cast<int>(people_.size()) < count; ++attempt) {
            const int w = static_cast<int>(unit(rng_) * static_cast<float>(walkways_.size())) % static_cast<int>(walkways_.size());
            const Walkway& way = walkways_[static_cast<std::size_t>(w)];
            const float s = way.s0 + unit(rng_) * (way.s1 - way.s0);
            const auto& road = world_.Roads().Roads()[static_cast<std::size_t>(way.road)];
            const Vector3 at = road.curve.Evaluate(s).position;
            const float d = Vector3::Distance(at, focus);
            if (d < 60.0f && !firstFill) continue;   // not out of thin air in front of the driver
            if (d > 260.0f) continue;
            Spawn(w, s, unit(rng_) < 0.5f ? -1.0f : 1.0f);
        }

        for (auto& p : people_) {
            const Walkway& way = walkways_[static_cast<std::size_t>(p.walkway)];
            // A car driven at a person makes them jump back onto the pavement they came from.
            if (player.valid && std::fabs(player.speed) > 1.0f && Vector3::Distance(player.position, p.position) < 3.2f) {
                p.crossing = -1;
                p.waitAtKerb = 0.0f;
                p.lateral = way.lateral;
            }
            if (p.crossing != -1) {
                const float kerb = p.crossHalf + 0.4f;
                if (std::fabs(p.lateral) >= kerb - 0.01f && std::fabs(p.lateral - p.crossFrom) < 0.02f) {
                    // At the kerb, waiting for a gap.
                    p.waitAtKerb += dt;
                    if (GapToCross(p, vehicles, player) || (p.crossing >= 0 && p.waitAtKerb > 45.0f)) {
                        p.lateral -= (p.crossFrom > 0.0f ? 1.0f : -1.0f) * 0.05f;   // step off
                    }
                } else {
                    p.lateral -= (p.crossFrom > 0.0f ? 1.0f : -1.0f) * p.speed * dt;
                    p.phase += p.speed * dt * 4.2f;
                    if (std::fabs(p.lateral) >= kerb && (p.lateral > 0.0f) != (p.crossFrom > 0.0f)) {
                        // Over: carry on along the pavement on this side.
                        const float side = p.lateral > 0.0f ? 1.0f : -1.0f;
                        int other = p.walkway;
                        for (std::size_t i = 0; i < walkways_.size(); ++i) {
                            const auto& o = walkways_[i];
                            if (o.road == way.road && (o.lateral > 0.0f) == (side > 0.0f) && p.s >= o.s0 && p.s <= o.s1) other = static_cast<int>(i);
                        }
                        p.walkway = other;
                        p.lateral = walkways_[static_cast<std::size_t>(other)].lateral;
                        p.crossing = -1;
                        p.walkedSinceCross = 0.0f;
                        p.nextCrossChance = unit(rng_);
                    }
                }
                UpdatePose(p);
                continue;
            }
            // Along the pavement; turn round at the end.
            p.s += p.direction * p.speed * dt;
            p.phase += p.speed * dt * 4.2f;
            if (p.s < way.s0 || p.s > way.s1) {
                p.s = std::clamp(p.s, way.s0, way.s1);
                p.direction = -p.direction;
            }
            // At a zebra crossing: about one in three goes over.
            for (std::size_t i = 0; i < crossings_.size(); ++i) {
                const auto& c = crossings_[i];
                if (c.road != way.road || std::fabs(c.s - p.s) > 0.6f) continue;
                if (p.nextCrossChance < 0.35f) {
                    p.crossing = static_cast<int>(i);
                    p.s = c.s;
                    p.crossHalf = c.halfWidth;
                    p.crossFrom = (way.lateral > 0.0f ? 1.0f : -1.0f) * (c.halfWidth + 0.4f);
                    p.lateral = p.crossFrom;
                    p.waitAtKerb = 0.0f;
                }
                p.nextCrossChance = 1.0f;   // one decision per visit
                break;
            }
            // In a village street people also cross where they please, now and then.
            p.walkedSinceCross += p.speed * dt;
            if (p.crossing == -1 && p.walkedSinceCross > 150.0f) {
                p.walkedSinceCross = 0.0f;
                if (unit(rng_) < 0.25f) {
                    const auto& road = world_.Roads().Roads()[static_cast<std::size_t>(way.road)];
                    p.crossing = -2;
                    p.crossHalf = road.profile.HalfPavedWidth();
                    p.crossFrom = (way.lateral > 0.0f ? 1.0f : -1.0f) * (p.crossHalf + 0.4f);
                    p.lateral = p.crossFrom;
                    p.waitAtKerb = 0.0f;
                }
            }
            if (p.nextCrossChance >= 1.0f && std::fabs(p.s - way.s0) > 1.0f && std::fabs(p.s - way.s1) > 1.0f) {
                bool nearAny = false;
                for (const auto& c : crossings_) nearAny = nearAny || (c.road == way.road && std::fabs(c.s - p.s) < 3.0f);
                if (!nearAny) p.nextCrossChance = unit(rng_);
            }
            UpdatePose(p);
        }
    }

    std::vector<PlayerProbe> Pedestrians::RoadProbes() const
    {
        std::vector<PlayerProbe> probes;
        for (const auto& p : people_) {
            // On the road, or at the kerb about to step off.
            if (p.crossing == -1) continue;
            PlayerProbe probe;
            probe.valid = true;
            probe.position = p.position;
            probe.lengthM = 0.6f;
            probe.speed = 0.0f;
            probes.push_back(probe);
        }
        return probes;
    }
}
