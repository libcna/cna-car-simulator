#include "CarSim/Map/LaneGraph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>

namespace CarSim::Map
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        float WrapAngle(float a)
        {
            while (a > kPi) a -= 2.0f * kPi;
            while (a <= -kPi) a += 2.0f * kPi;
            return a;
        }

        float HeadingOf(const Vector3& t) { return std::atan2(t.X, -t.Z); }

        Vector2 Flat(const Vector3& v) { return Vector2(v.X, v.Z); }

        bool SegmentsIntersect(const Vector2& p1, const Vector2& p2, const Vector2& q1, const Vector2& q2)
        {
            const auto orient = [](const Vector2& a, const Vector2& b, const Vector2& c) {
                const float v = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
                return v > 1e-6f ? 1 : (v < -1e-6f ? -1 : 0);
            };
            const int o1 = orient(p1, p2, q1);
            const int o2 = orient(p1, p2, q2);
            const int o3 = orient(q1, q2, p1);
            const int o4 = orient(q1, q2, p2);
            return o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
        }

        bool PolylinesCross(const std::vector<LanePoint>& a, const std::vector<LanePoint>& b)
        {
            for (std::size_t i = 0; i + 1 < a.size(); ++i) {
                for (std::size_t j = 0; j + 1 < b.size(); ++j) {
                    if (SegmentsIntersect(Flat(a[i].position), Flat(a[i + 1].position), Flat(b[j].position), Flat(b[j + 1].position))) {
                        return true;
                    }
                }
            }
            return false;
        }

        float PolylineDistance(const std::vector<LanePoint>& a, const std::vector<LanePoint>& b)
        {
            float best = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < a.size(); i += 2) {
                for (std::size_t j = 0; j + 1 < b.size(); ++j) {
                    float t = 0.0f;
                    best = std::min(best, DistanceToSegment(Flat(a[i].position), Flat(b[j].position), Flat(b[j + 1].position), t));
                }
            }
            return best;
        }

        LanePoint EvaluatePolyline(const std::vector<LanePoint>& points, const float s)
        {
            if (points.empty()) {
                return {};
            }
            if (s <= points.front().s) return points.front();
            if (s >= points.back().s) return points.back();
            const auto it = std::lower_bound(points.begin(), points.end(), s, [](const LanePoint& p, const float v) { return p.s < v; });
            const std::size_t i1 = static_cast<std::size_t>(it - points.begin());
            const std::size_t i0 = i1 - 1;
            const LanePoint& a = points[i0];
            const LanePoint& b = points[i1];
            const float t = (s - a.s) / std::max(1e-6f, b.s - a.s);
            LanePoint r;
            r.position = a.position + (b.position - a.position) * t;
            r.tangent = a.tangent + (b.tangent - a.tangent) * t;
            if (r.tangent.LengthSquared() > 1e-10f) r.tangent.Normalize();
            r.s = s;
            r.speedLimitKmh = t < 0.5f ? a.speedLimitKmh : b.speedLimitKmh;
            r.curvature = a.curvature + (b.curvature - a.curvature) * t;
            return r;
        }

        void Finish(std::vector<LanePoint>& points, float& length)
        {
            float s = 0.0f;
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (i > 0) {
                    s += Vector3::Distance(points[i].position, points[i - 1].position);
                }
                points[i].s = s;
            }
            length = s;
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (points[i].tangent.LengthSquared() < 1e-8f && points.size() > 1) {
                    const std::size_t a = i > 0 ? i - 1 : i;
                    const std::size_t b = i + 1 < points.size() ? i + 1 : i;
                    Vector3 d = points[b].position - points[a].position;
                    if (d.LengthSquared() > 1e-10f) d.Normalize();
                    points[i].tangent = d;
                }
            }
        }
    }

    const char* ToString(const TurnType t)
    {
        switch (t) {
            case TurnType::Straight: return "straight";
            case TurnType::Left: return "left";
            case TurnType::Right: return "right";
            case TurnType::UTurn: return "u-turn";
        }
        return "?";
    }

    LanePoint Lane::Evaluate(const float s) const { return EvaluatePolyline(points, s); }
    LanePoint LaneLink::Evaluate(const float s) const { return EvaluatePolyline(points, s); }

    float Lane::Project(const Vector2& point, float& lateral) const
    {
        float best = std::numeric_limits<float>::max();
        float bestS = 0.0f;
        lateral = 0.0f;
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const Vector2 a = Flat(points[i].position);
            const Vector2 b = Flat(points[i + 1].position);
            float t = 0.0f;
            const float d = DistanceToSegment(point, a, b, t);
            if (d < best) {
                best = d;
                bestS = points[i].s + (points[i + 1].s - points[i].s) * t;
                Vector2 dir = b - a;
                if (dir.LengthSquared() > 1e-10f) dir.Normalize();
                const Vector2 proj = a + (b - a) * t;
                lateral = Vector2::Dot(point - proj, Vector2(-dir.Y, dir.X));
            }
        }
        return bestS;
    }

    // ------------------------------------------------------------------ build

    void LaneGraph::Build(const RoadNetwork& network)
    {
        lanes_.clear();
        links_.clear();
        BuildLanes(network);
        BuildLinks(network);
        BuildConflicts(network);

        float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
        for (const auto& lane : lanes_) {
            for (const auto& p : lane.points) {
                minX = std::min(minX, p.position.X); maxX = std::max(maxX, p.position.X);
                minZ = std::min(minZ, p.position.Z); maxZ = std::max(maxZ, p.position.Z);
            }
        }
        if (lanes_.empty()) {
            minX = minZ = -1.0f; maxX = maxZ = 1.0f;
        }
        grid_.Reset(minX - 20.0f, minZ - 20.0f, maxX + 20.0f, maxZ + 20.0f, 20.0f);
        for (const auto& lane : lanes_) {
            for (std::size_t i = 0; i + 1 < lane.points.size(); ++i) {
                const auto& a = lane.points[i].position;
                const auto& b = lane.points[i + 1].position;
                grid_.Insert(lane.id * 65536 + static_cast<int>(i), std::min(a.X, b.X) - 4.0f, std::min(a.Z, b.Z) - 4.0f,
                             std::max(a.X, b.X) + 4.0f, std::max(a.Z, b.Z) + 4.0f);
            }
        }
    }

    void LaneGraph::BuildLanes(const RoadNetwork& network)
    {
        const auto& roads = network.Roads();
        for (std::size_t pi = 0; pi < network.Pieces().size(); ++pi) {
            const RoadPiece& piece = network.Pieces()[pi];
            const Road& road = roads[static_cast<std::size_t>(piece.road)];
            const auto& samples = road.curve.Samples();
            // s values along the piece: exact ends plus the interior samples.
            std::vector<float> stations;
            stations.push_back(piece.s0);
            for (std::size_t i = piece.sampleBegin; i < piece.sampleEnd; ++i) {
                if (samples[i].s > piece.s0 + 0.2f && samples[i].s < piece.s1 - 0.2f) {
                    stations.push_back(samples[i].s);
                }
            }
            stations.push_back(piece.s1);

            const int lanesPerDir = road.profile.lanesPerDirection;
            const float laneWidth = road.profile.laneWidth;
            const int firstLane = static_cast<int>(lanes_.size());
            for (int dir = 0; dir < 2; ++dir) {
                const bool forward = dir == 0;
                if (!forward && road.spec->oneWay) {
                    continue;
                }
                for (int j = 0; j < lanesPerDir; ++j) {
                    Lane lane;
                    lane.id = static_cast<int>(lanes_.size());
                    lane.road = piece.road;
                    lane.piece = static_cast<int>(pi);
                    lane.forward = forward;
                    lane.width = laneWidth;
                    lane.lateralOffset = (forward ? 1.0f : -1.0f) * laneWidth * (static_cast<float>(j) + 0.5f);
                    for (const float s : stations) {
                        RoadHit hit;
                        hit.road = piece.road;
                        hit.s = s;
                        hit.lateral = lane.lateralOffset;
                        hit.sample = road.curve.Evaluate(s);
                        const Vector3 right(-hit.sample.tangent.Z, 0.0f, hit.sample.tangent.X);
                        LanePoint p;
                        p.position = hit.sample.position + right * lane.lateralOffset;
                        p.position.Y = network.SurfaceHeight(hit);
                        p.tangent = forward ? hit.sample.tangent : hit.sample.tangent * -1.0f;
                        p.speedLimitKmh = road.SpeedLimitAt(s);
                        p.curvature = forward ? hit.sample.curvature : -hit.sample.curvature;
                        lane.points.push_back(p);
                    }
                    if (!forward) {
                        std::reverse(lane.points.begin(), lane.points.end());
                    }
                    Finish(lane.points, lane.length);
                    lane.fromIntersection = forward ? piece.startIntersection : piece.endIntersection;
                    lane.toIntersection = forward ? piece.endIntersection : piece.startIntersection;
                    lanes_.push_back(std::move(lane));
                }
            }
            // Opposite-lane pairing (innermost lanes of both directions).
            const int lastLane = static_cast<int>(lanes_.size());
            if (!road.spec->oneWay && lastLane - firstLane >= 2 * lanesPerDir) {
                for (int j = 0; j < lanesPerDir; ++j) {
                    Lane& f = lanes_[static_cast<std::size_t>(firstLane + j)];
                    Lane& b = lanes_[static_cast<std::size_t>(firstLane + lanesPerDir + j)];
                    f.oppositeLane = b.id;
                    b.oppositeLane = f.id;
                }
            }
        }
    }

    void LaneGraph::BuildLinks(const RoadNetwork& network)
    {
        const auto& intersections = network.Intersections();
        for (std::size_t ii = 0; ii < intersections.size(); ++ii) {
            const Intersection& inter = intersections[ii];
            for (std::size_t ai = 0; ai < inter.approaches.size(); ++ai) {
                const Approach& in = inter.approaches[ai];
                if (in.piece < 0) continue;
                for (std::size_t bi = 0; bi < inter.approaches.size(); ++bi) {
                    if (ai == bi) continue;
                    const Approach& out = inter.approaches[bi];
                    if (out.piece < 0) continue;
                    // Incoming lanes: end at this intersection on approach `in`'s piece.
                    for (const Lane& from : lanes_) {
                        if (from.piece != in.piece || from.toIntersection != static_cast<int>(ii)) continue;
                        for (const Lane& to : lanes_) {
                            if (to.piece != out.piece || to.fromIntersection != static_cast<int>(ii)) continue;
                            LaneLink link;
                            link.id = static_cast<int>(links_.size());
                            link.fromLane = from.id;
                            link.toLane = to.id;
                            link.intersection = static_cast<int>(ii);
                            link.control = in.control;
                            link.priority = in.control == ApproachControl::Priority;
                            const float hIn = HeadingOf(from.points.back().tangent);
                            const float hOut = HeadingOf(to.points.front().tangent);
                            const float delta = WrapAngle(hOut - hIn);
                            const float deg = delta * 180.0f / kPi;
                            if (std::fabs(deg) < 30.0f) link.turn = TurnType::Straight;
                            else if (std::fabs(deg) > 150.0f) link.turn = TurnType::UTurn;
                            else link.turn = deg > 0.0f ? TurnType::Right : TurnType::Left;
                            if (link.turn == TurnType::UTurn && inter.approaches.size() > 2) {
                                continue;   // no U-turns at real intersections
                            }
                            // Cubic Hermite connector.
                            const Vector3 p0 = from.points.back().position;
                            const Vector3 p1 = to.points.front().position;
                            const float dist = Vector3::Distance(p0, p1);
                            const Vector3 t0 = from.points.back().tangent * dist;
                            const Vector3 t1 = to.points.front().tangent * dist;
                            const int steps = std::max(4, static_cast<int>(dist / 1.5f));
                            for (int k = 0; k <= steps; ++k) {
                                const float t = static_cast<float>(k) / static_cast<float>(steps);
                                const float t2 = t * t;
                                const float t3 = t2 * t;
                                const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                                const float h10 = t3 - 2.0f * t2 + t;
                                const float h01 = -2.0f * t3 + 3.0f * t2;
                                const float h11 = t3 - t2;
                                LanePoint p;
                                p.position = p0 * h00 + t0 * h10 + p1 * h01 + t1 * h11;
                                p.position.Y = inter.PlaneHeight(Vector2(p.position.X, p.position.Z));
                                const float d00 = 6.0f * t2 - 6.0f * t;
                                const float d10 = 3.0f * t2 - 4.0f * t + 1.0f;
                                const float d01 = -6.0f * t2 + 6.0f * t;
                                const float d11 = 3.0f * t2 - 2.0f * t;
                                p.tangent = p0 * d00 + t0 * d10 + p1 * d01 + t1 * d11;
                                if (p.tangent.LengthSquared() > 1e-10f) p.tangent.Normalize();
                                p.speedLimitKmh = std::min(from.points.back().speedLimitKmh, to.points.front().speedLimitKmh);
                                if (link.turn != TurnType::Straight) {
                                    p.speedLimitKmh = std::min(p.speedLimitKmh, 25.0f);
                                }
                                link.points.push_back(p);
                            }
                            Finish(link.points, link.length);
                            // Curvature estimate from the turn angle over the connector length.
                            const float curvature = link.length > 0.5f ? delta / link.length : 0.0f;
                            for (auto& p : link.points) p.curvature = curvature;
                            links_.push_back(std::move(link));
                        }
                    }
                }
            }
        }
        // U-turns at dead ends: connect a lane that ends nowhere to its opposite lane.
        for (std::size_t li = 0; li < lanes_.size(); ++li) {
            const Lane& lane = lanes_[li];
            if (lane.toIntersection >= 0 || lane.oppositeLane < 0) continue;
            const Lane& back = lanes_[static_cast<std::size_t>(lane.oppositeLane)];
            LaneLink link;
            link.id = static_cast<int>(links_.size());
            link.fromLane = lane.id;
            link.toLane = back.id;
            link.intersection = -1;
            link.turn = TurnType::UTurn;
            link.control = ApproachControl::Priority;
            link.priority = true;
            const Vector3 p0 = lane.points.back().position;
            const Vector3 p1 = back.points.front().position;
            const Vector3 mid = (p0 + p1) * 0.5f + lane.points.back().tangent * (Vector3::Distance(p0, p1) * 0.6f + 1.0f);
            for (int k = 0; k <= 8; ++k) {
                const float t = static_cast<float>(k) / 8.0f;
                const float u = 1.0f - t;
                LanePoint p;
                p.position = p0 * (u * u) + mid * (2.0f * u * t) + p1 * (t * t);
                p.speedLimitKmh = 10.0f;
                link.points.push_back(p);
            }
            Finish(link.points, link.length);
            links_.push_back(std::move(link));
        }
        for (const auto& link : links_) {
            lanes_[static_cast<std::size_t>(link.fromLane)].outgoingLinks.push_back(link.id);
            lanes_[static_cast<std::size_t>(link.toLane)].incomingLinks.push_back(link.id);
        }
    }

    void LaneGraph::BuildConflicts(const RoadNetwork& network)
    {
        const auto& intersections = network.Intersections();
        // Group links per intersection.
        std::vector<std::vector<int>> perIntersection(intersections.size());
        for (const auto& link : links_) {
            if (link.intersection >= 0) {
                perIntersection[static_cast<std::size_t>(link.intersection)].push_back(link.id);
            }
        }
        for (std::size_t ii = 0; ii < intersections.size(); ++ii) {
            const auto& ids = perIntersection[ii];
            for (std::size_t x = 0; x < ids.size(); ++x) {
                LaneLink& L = links_[static_cast<std::size_t>(ids[x])];
                const Lane& lFrom = lanes_[static_cast<std::size_t>(L.fromLane)];
                for (std::size_t y = 0; y < ids.size(); ++y) {
                    if (x == y) continue;
                    const LaneLink& M = links_[static_cast<std::size_t>(ids[y])];
                    const Lane& mFrom = lanes_[static_cast<std::size_t>(M.fromLane)];
                    if (mFrom.piece == lFrom.piece) continue;   // same approach: no conflict handling
                    const bool merge = L.toLane == M.toLane;
                    const bool cross = PolylinesCross(L.points, M.points) || PolylineDistance(L.points, M.points) < 1.6f;
                    if (!merge && !cross) continue;
                    L.conflicts.push_back(M.id);

                    bool yield = false;
                    if (L.priority && !M.priority) {
                        yield = false;
                    } else if (!L.priority && M.priority) {
                        yield = true;
                    }
                    // Same class of right of way: geometry decides.
                    const float hL = HeadingOf(lFrom.points.back().tangent);          // our travel heading into the node
                    const float hM = HeadingOf(mFrom.points.back().tangent);
                    const float rel = WrapAngle(hM - hL);                              // M's heading relative to ours
                    const bool oncoming = std::fabs(rel) > 150.0f * kPi / 180.0f;
                    const bool mFromRight = rel < -30.0f * kPi / 180.0f && rel > -150.0f * kPi / 180.0f;
                    if (L.priority == M.priority) {
                        if (L.turn == TurnType::Left && oncoming && M.turn != TurnType::Left) {
                            yield = true;   // left turn gives way to oncoming traffic
                        } else if (L.turn == TurnType::Left && oncoming && M.turn == TurnType::Left) {
                            yield = false;
                        } else if (!oncoming && mFromRight) {
                            // Right-hand rule; it covers merges too: the car from the right goes first.
                            yield = true;
                        }
                    } else if (!L.priority && M.priority) {
                        yield = true;
                    }
                    if (yield) {
                        L.yieldTo.push_back(M.id);
                    }
                }
            }
            // Right of way must be antisymmetric, otherwise two cars wait for each other forever.
            // Should odd geometry produce a mutual yield, the turning movement gives way (the
            // higher link id if both turn or both go straight).
            for (const int a : ids) {
                LaneLink& A = links_[static_cast<std::size_t>(a)];
                const std::vector<int> yields = A.yieldTo;
                for (const int b : yields) {
                    LaneLink& B = links_[static_cast<std::size_t>(b)];
                    if (std::find(B.yieldTo.begin(), B.yieldTo.end(), A.id) == B.yieldTo.end()) continue;
                    const bool aTurns = A.turn != TurnType::Straight;
                    const bool bTurns = B.turn != TurnType::Straight;
                    const bool aKeepsYielding = aTurns == bTurns ? A.id > B.id : aTurns;
                    std::vector<int>& drop = aKeepsYielding ? B.yieldTo : A.yieldTo;
                    const int victim = aKeepsYielding ? A.id : B.id;
                    drop.erase(std::remove(drop.begin(), drop.end(), victim), drop.end());
                }
            }
        }
    }

    // ------------------------------------------------------------------ queries

    int LaneGraph::NearestLane(const Vector2& point, const float headingRad, const float maxDistance, float* outS, float* outLateral) const
    {
        int best = -1;
        float bestScore = std::numeric_limits<float>::max();
        float bestS = 0.0f;
        float bestLateral = 0.0f;
        grid_.Query(point.X - maxDistance, point.Y - maxDistance, point.X + maxDistance, point.Y + maxDistance, [&](const std::int32_t id) {
            const int laneId = id / 65536;
            const std::size_t seg = static_cast<std::size_t>(id % 65536);
            const Lane& lane = lanes_[static_cast<std::size_t>(laneId)];
            if (seg + 1 >= lane.points.size()) return;
            const Vector2 a = Flat(lane.points[seg].position);
            const Vector2 b = Flat(lane.points[seg + 1].position);
            float t = 0.0f;
            const float d = DistanceToSegment(point, a, b, t);
            if (d > maxDistance) return;
            const float laneHeading = HeadingOf(lane.points[seg].tangent);
            const float misalign = 1.0f - std::cos(WrapAngle(laneHeading - headingRad));
            const float score = d + misalign * 6.0f;
            if (score < bestScore) {
                bestScore = score;
                best = laneId;
                bestS = lane.points[seg].s + (lane.points[seg + 1].s - lane.points[seg].s) * t;
                Vector2 dir = b - a;
                if (dir.LengthSquared() > 1e-10f) dir.Normalize();
                bestLateral = Vector2::Dot(point - (a + (b - a) * t), Vector2(-dir.Y, dir.X));
            }
        });
        if (outS) *outS = bestS;
        if (outLateral) *outLateral = bestLateral;
        return best;
    }

    std::vector<RouteStep> LaneGraph::FindRoute(const int fromLane, const int toLane) const
    {
        std::vector<RouteStep> route;
        if (fromLane < 0 || toLane < 0) {
            return route;
        }
        const std::size_t n = lanes_.size();
        std::vector<float> dist(n, std::numeric_limits<float>::max());
        std::vector<int> prevLane(n, -1);
        std::vector<int> prevLink(n, -1);
        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
        dist[static_cast<std::size_t>(fromLane)] = 0.0f;
        queue.emplace(0.0f, fromLane);
        while (!queue.empty()) {
            const auto [d, lane] = queue.top();
            queue.pop();
            if (d > dist[static_cast<std::size_t>(lane)]) continue;
            if (lane == toLane) break;
            for (const int linkId : lanes_[static_cast<std::size_t>(lane)].outgoingLinks) {
                const LaneLink& link = links_[static_cast<std::size_t>(linkId)];
                const float nd = d + link.length + lanes_[static_cast<std::size_t>(link.toLane)].length;
                if (nd < dist[static_cast<std::size_t>(link.toLane)]) {
                    dist[static_cast<std::size_t>(link.toLane)] = nd;
                    prevLane[static_cast<std::size_t>(link.toLane)] = lane;
                    prevLink[static_cast<std::size_t>(link.toLane)] = linkId;
                    queue.emplace(nd, link.toLane);
                }
            }
        }
        if (dist[static_cast<std::size_t>(toLane)] == std::numeric_limits<float>::max()) {
            return route;
        }
        std::vector<RouteStep> reversed;
        int cur = toLane;
        reversed.push_back({cur, -1});
        while (cur != fromLane) {
            const int link = prevLink[static_cast<std::size_t>(cur)];
            cur = prevLane[static_cast<std::size_t>(cur)];
            reversed.push_back({cur, link});
        }
        route.assign(reversed.rbegin(), reversed.rend());
        return route;
    }

    std::vector<int> LaneGraph::Reachable(const int fromLane) const
    {
        std::vector<int> result;
        if (fromLane < 0 || static_cast<std::size_t>(fromLane) >= lanes_.size()) {
            return result;
        }
        std::vector<bool> seen(lanes_.size(), false);
        std::vector<int> stack{fromLane};
        seen[static_cast<std::size_t>(fromLane)] = true;
        while (!stack.empty()) {
            const int lane = stack.back();
            stack.pop_back();
            result.push_back(lane);
            for (const int linkId : lanes_[static_cast<std::size_t>(lane)].outgoingLinks) {
                const int next = links_[static_cast<std::size_t>(linkId)].toLane;
                if (!seen[static_cast<std::size_t>(next)]) {
                    seen[static_cast<std::size_t>(next)] = true;
                    stack.push_back(next);
                }
            }
        }
        return result;
    }

    int LaneGraph::RandomLink(const int lane, std::mt19937& rng) const
    {
        const auto& out = lanes_[static_cast<std::size_t>(lane)].outgoingLinks;
        if (out.empty()) {
            return -1;
        }
        std::vector<float> weights;
        float total = 0.0f;
        for (const int id : out) {
            const LaneLink& link = links_[static_cast<std::size_t>(id)];
            float w = link.turn == TurnType::Straight ? 3.0f : (link.turn == TurnType::UTurn ? 0.2f : 1.0f);
            weights.push_back(w);
            total += w;
        }
        std::uniform_real_distribution<float> pick(0.0f, total);
        float r = pick(rng);
        for (std::size_t i = 0; i < out.size(); ++i) {
            r -= weights[i];
            if (r <= 0.0f) {
                return out[i];
            }
        }
        return out.back();
    }

    float LaneGraph::TotalLaneLength() const
    {
        float total = 0.0f;
        for (const auto& lane : lanes_) {
            total += lane.length;
        }
        return total;
    }
}
