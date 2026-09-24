#include "CarSim/Map/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace CarSim::Map
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        float Heading(const Vector2& d) { return std::atan2(d.X, -d.Y); }

        Vector2 Perp(const Vector2& d) { return Vector2(-d.Y, d.X); }   // right-hand side of a direction (x east, z south)

        Vector2 Normalized(const Vector2& v)
        {
            const float len = v.Length();
            return len > 1e-9f ? v * (1.0f / len) : Vector2(0.0f, -1.0f);
        }

        float SmoothStep(const float t) { const float c = std::clamp(t, 0.0f, 1.0f); return c * c * (3.0f - 2.0f * c); }
    }

    // ------------------------------------------------------------------ utilities

    bool PointInPolygon(const Vector2& p, const std::vector<Vector2>& polygon)
    {
        bool inside = false;
        const std::size_t n = polygon.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vector2& a = polygon[i];
            const Vector2& b = polygon[j];
            if (((a.Y > p.Y) != (b.Y > p.Y)) && (p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X)) {
                inside = !inside;
            }
        }
        return inside;
    }

    float PolygonArea(const std::vector<Vector2>& polygon)
    {
        float area = 0.0f;
        const std::size_t n = polygon.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            area += polygon[j].X * polygon[i].Y - polygon[i].X * polygon[j].Y;
        }
        return area * 0.5f;
    }

    float DistanceToSegment(const Vector2& p, const Vector2& a, const Vector2& b, float& t)
    {
        const Vector2 ab = b - a;
        const float len2 = ab.LengthSquared();
        t = len2 > 1e-12f ? std::clamp(Vector2::Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        return Vector2::Distance(p, a + ab * t);
    }

    // ------------------------------------------------------------------ RoadProfile

    float RoadProfile::HalfTotalWidth() const
    {
        const float hp = HalfPavedWidth();
        float outer = hp + shoulderWidth;
        if (sidewalk.width > 0.0f && (sidewalk.left || sidewalk.right)) {
            outer = std::max(outer, hp + sidewalk.width);
        }
        return outer;
    }

    float RoadProfile::SidewalkOuter(const bool right) const
    {
        if (sidewalk.width <= 0.0f || !(right ? sidewalk.right : sidewalk.left)) {
            return 0.0f;
        }
        return HalfPavedWidth() + sidewalk.width;
    }

    // ------------------------------------------------------------------ RoadCurve

    void RoadCurve::Build(const std::vector<Vector2>& controlPoints, const std::vector<float>& cornerRadii,
                          const std::vector<bool>& urbanFlags, const float sampleSpacing, std::vector<float>& outControlS)
    {
        samples_.clear();
        outControlS.assign(controlPoints.size(), 0.0f);
        const std::size_t n = controlPoints.size();
        if (n < 2) {
            return;
        }
        // Tangent lengths of the fillets at interior points, limited by the neighbouring segments.
        std::vector<float> tangentLen(n, 0.0f);
        std::vector<float> radius(n, 0.0f);
        std::vector<float> turn(n, 0.0f);   // signed turn angle (positive = right)
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const Vector2 d1 = Normalized(controlPoints[i] - controlPoints[i - 1]);
            const Vector2 d2 = Normalized(controlPoints[i + 1] - controlPoints[i]);
            const float cross = d1.X * d2.Y - d1.Y * d2.X;
            const float dot = std::clamp(Vector2::Dot(d1, d2), -1.0f, 1.0f);
            const float theta = std::acos(dot);
            turn[i] = cross >= 0.0f ? theta : -theta;
            if (theta < 0.5f * kPi / 180.0f || cornerRadii[i] <= 0.0f) {
                continue;
            }
            float r = cornerRadii[i];
            float t = r * std::tan(theta * 0.5f);
            const float len1 = Vector2::Distance(controlPoints[i], controlPoints[i - 1]);
            const float len2 = Vector2::Distance(controlPoints[i + 1], controlPoints[i]);
            const float tMax = 0.5f * std::min(len1, len2) - 0.05f;
            if (t > tMax) {
                t = std::max(0.0f, tMax);
                r = t / std::max(1e-4f, std::tan(theta * 0.5f));
            }
            tangentLen[i] = t;
            radius[i] = r;
        }

        float s = 0.0f;
        const auto push = [&](const Vector2& p, const Vector2& dir, const float curvature, const bool urban, const bool force) {
            if (!samples_.empty() && !force) {
                const float ds = Vector2::Distance(Vector2(samples_.back().position.X, samples_.back().position.Z), p);
                if (ds < 1e-4f) {
                    return;
                }
            }
            if (!samples_.empty()) {
                s += Vector2::Distance(Vector2(samples_.back().position.X, samples_.back().position.Z), p);
            }
            RoadSample sample;
            sample.position = Vector3(p.X, 0.0f, p.Y);
            sample.tangent = Vector3(dir.X, 0.0f, dir.Y);
            sample.s = s;
            sample.curvature = curvature;
            sample.urban = urban;
            samples_.push_back(sample);
        };

        Vector2 current = controlPoints[0];
        outControlS[0] = 0.0f;
        for (std::size_t i = 1; i < n; ++i) {
            const Vector2 target = controlPoints[i];
            const Vector2 dir = Normalized(target - current);
            const bool segmentUrban = urbanFlags[i - 1] && urbanFlags[i];
            // Straight part of this segment: from `current` to the start of the fillet at i.
            const Vector2 lineEnd = (i + 1 < n) ? target - dir * tangentLen[i] : target;
            const float lineLen = Vector2::Distance(current, lineEnd);
            const int steps = std::max(1, static_cast<int>(std::ceil(lineLen / sampleSpacing)));
            for (int k = 0; k <= steps; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(steps);
                push(current + (lineEnd - current) * t, dir, 0.0f, segmentUrban, k == steps);
            }
            if (i + 1 < n) {
                if (tangentLen[i] > 1e-4f) {
                    // Circular arc from lineEnd to target + d2 * t.
                    const Vector2 d2 = Normalized(controlPoints[i + 1] - target);
                    const float theta = std::fabs(turn[i]);
                    const float r = radius[i];
                    const float sign = turn[i] >= 0.0f ? 1.0f : -1.0f;   // right turn: centre on the right
                    const Vector2 centre = lineEnd + Perp(dir) * (sign * r);
                    const float arcLen = r * theta;
                    const int arcSteps = std::max(2, static_cast<int>(std::ceil(arcLen / sampleSpacing)));
                    const float startAngle = std::atan2(lineEnd.Y - centre.Y, lineEnd.X - centre.X);
                    // Rotating the radius vector: right turn (centre on the right) means the point moves
                    // clockwise on paper (x east, z south) which is a positive angle increment here.
                    const float sweep = sign * theta;
                    for (int k = 1; k <= arcSteps; ++k) {
                        const float a = startAngle + sweep * static_cast<float>(k) / static_cast<float>(arcSteps);
                        const Vector2 p = centre + Vector2(std::cos(a), std::sin(a)) * r;
                        const Vector2 radial = Normalized(p - centre);
                        // Tangent is the radial rotated by +/-90 degrees depending on the turn direction.
                        const Vector2 tangent = sign > 0.0f ? Vector2(-radial.Y, radial.X) : Vector2(radial.Y, -radial.X);
                        if (k == arcSteps / 2) {
                            outControlS[i] = s + Vector2::Distance(Vector2(samples_.back().position.X, samples_.back().position.Z), p);
                        }
                        push(p, tangent, sign / r, urbanFlags[i], k == arcSteps);
                    }
                    if (arcSteps / 2 == 0) {
                        outControlS[i] = s;
                    }
                    current = target + d2 * tangentLen[i];
                    // The arc's end point is the new current point; make sure we did not drift.
                    const Vector2 arcEnd(samples_.back().position.X, samples_.back().position.Z);
                    current = arcEnd;
                } else {
                    outControlS[i] = s;
                    current = target;
                }
            } else {
                outControlS[i] = s;
            }
        }
        // Fix tangents of the two end samples and any degenerate ones.
        for (std::size_t i = 0; i < samples_.size(); ++i) {
            if (samples_[i].tangent.LengthSquared() < 1e-8f) {
                const std::size_t j = i + 1 < samples_.size() ? i + 1 : i - 1;
                Vector3 d = samples_[j].position - samples_[i].position;
                if (j < i) d = d * -1.0f;
                d.Normalize();
                samples_[i].tangent = d;
            }
        }
    }

    void RoadCurve::SetHeights(const std::vector<float>& heights)
    {
        for (std::size_t i = 0; i < samples_.size() && i < heights.size(); ++i) {
            samples_[i].position.Y = heights[i];
        }
        for (std::size_t i = 0; i < samples_.size(); ++i) {
            const std::size_t a = i > 0 ? i - 1 : i;
            const std::size_t b = i + 1 < samples_.size() ? i + 1 : i;
            if (a == b) {
                continue;
            }
            Vector3 planar = samples_[i].tangent;
            planar.Y = 0.0f;
            const float run = samples_[b].s - samples_[a].s;
            const float rise = samples_[b].position.Y - samples_[a].position.Y;
            Vector3 t = planar + Vector3(0.0f, run > 1e-4f ? rise / run : 0.0f, 0.0f);
            t.Normalize();
            samples_[i].tangent = t;
        }
    }

    RoadSample RoadCurve::Evaluate(const float s) const
    {
        if (samples_.empty()) {
            return {};
        }
        if (s <= samples_.front().s) {
            return samples_.front();
        }
        if (s >= samples_.back().s) {
            return samples_.back();
        }
        const auto it = std::lower_bound(samples_.begin(), samples_.end(), s, [](const RoadSample& a, const float v) { return a.s < v; });
        const std::size_t i1 = static_cast<std::size_t>(it - samples_.begin());
        const std::size_t i0 = i1 - 1;
        const RoadSample& a = samples_[i0];
        const RoadSample& b = samples_[i1];
        const float t = (s - a.s) / std::max(1e-6f, b.s - a.s);
        RoadSample r;
        r.position = a.position + (b.position - a.position) * t;
        r.tangent = a.tangent + (b.tangent - a.tangent) * t;
        if (r.tangent.LengthSquared() > 1e-10f) r.tangent.Normalize();
        r.s = s;
        r.curvature = a.curvature + (b.curvature - a.curvature) * t;
        r.urban = t < 0.5f ? a.urban : b.urban;
        return r;
    }

    float RoadCurve::ProjectRange(const Vector2& point, const std::size_t sampleBegin, const std::size_t sampleEnd, float& s, float& lateral) const
    {
        float best = std::numeric_limits<float>::max();
        s = 0.0f;
        lateral = 0.0f;
        if (samples_.size() < 2) {
            return best;
        }
        const std::size_t end = std::min(sampleEnd, samples_.size());
        for (std::size_t i = sampleBegin; i + 1 < end; ++i) {
            const Vector2 a(samples_[i].position.X, samples_[i].position.Z);
            const Vector2 b(samples_[i + 1].position.X, samples_[i + 1].position.Z);
            float t = 0.0f;
            const float d = DistanceToSegment(point, a, b, t);
            if (d < best) {
                best = d;
                s = samples_[i].s + (samples_[i + 1].s - samples_[i].s) * t;
                const Vector2 dir = Normalized(b - a);
                const Vector2 proj = a + (b - a) * t;
                lateral = Vector2::Dot(point - proj, Perp(dir));
            }
        }
        return best;
    }

    float RoadCurve::Project(const Vector2& point, float& s, float& lateral) const
    {
        return ProjectRange(point, 0, samples_.size(), s, lateral);
    }

    // ------------------------------------------------------------------ Road

    float Road::SpeedLimitAt(const float s) const
    {
        const RoadSample sample = curve.Evaluate(s);
        return sample.urban ? std::min(spec->urbanSpeedLimitKmh, spec->speedLimitKmh) : spec->speedLimitKmh;
    }

    // ------------------------------------------------------------------ RoadNetwork

    bool RoadNetwork::Build(const MapData& data, const HeightSampler& terrainHeight, std::vector<std::string>& errors)
    {
        roads_.clear();
        intersections_.clear();
        pieces_.clear();
        nodePositions_.clear();
        nodeIntersection_.assign(data.nodes.size(), -1);
        BuildCurves(data, errors);
        if (!errors.empty()) {
            return false;
        }
        BuildIntersections(data);
        // Remember, per road, where it meets a junction, so the crossfall can ease out there.
        for (std::size_t ix = 0; ix < intersections_.size(); ++ix) {
            for (const Approach& a : intersections_[ix].approaches) {
                roads_[static_cast<std::size_t>(a.road)].junctions.push_back(
                    Road::JunctionRef{a.nodeS, a.setback, static_cast<int>(ix)});
            }
        }
        BuildHeights(data, terrainHeight);
        BuildPieces();
        BuildGrid();
        return errors.empty();
    }

    void RoadNetwork::BuildCurves(const MapData& data, std::vector<std::string>& errors)
    {
        std::map<std::string, int> nodeIndex;
        for (std::size_t i = 0; i < data.nodes.size(); ++i) {
            nodeIndex[data.nodes[i].id] = static_cast<int>(i);
            nodePositions_.emplace_back(data.nodes[i].position.X, 0.0f, data.nodes[i].position.Y);
        }
        std::vector<int> nodeUse(data.nodes.size(), 0);
        for (const auto& spec : data.roads) {
            for (const auto& id : spec.nodes) {
                const auto it = nodeIndex.find(id);
                if (it != nodeIndex.end()) {
                    ++nodeUse[static_cast<std::size_t>(it->second)];
                }
            }
        }
        for (std::size_t r = 0; r < data.roads.size(); ++r) {
            const RoadSpec& spec = data.roads[r];
            Road road;
            road.index = static_cast<int>(r);
            road.spec = &spec;
            std::vector<Vector2> points;
            std::vector<float> radii;
            std::vector<bool> urban;
            for (const auto& id : spec.nodes) {
                const auto it = nodeIndex.find(id);
                if (it == nodeIndex.end()) {
                    errors.push_back("road '" + spec.id + "': unknown node '" + id + "'");
                    continue;
                }
                const RoadNodeSpec& node = data.nodes[static_cast<std::size_t>(it->second)];
                road.nodeIndices.push_back(it->second);
                points.push_back(node.position);
                // Junction nodes stay sharp so that every road passes exactly through the node.
                const bool junction = nodeUse[static_cast<std::size_t>(it->second)] > 1;
                radii.push_back(junction ? 0.0f : (node.cornerRadius > 0.0f ? node.cornerRadius : spec.cornerRadius));
                urban.push_back(node.urban);
            }
            if (points.size() < 2) {
                errors.push_back("road '" + spec.id + "': needs at least two valid nodes");
                continue;
            }
            road.curve.Build(points, radii, urban, sampleSpacing_, road.nodeS);
            float previousSectionEnd = 0.0f;
            for (const auto& section : spec.centreLineSections) {
                if (section.fromM < previousSectionEnd || section.fromM < 0.0f || section.toM <= section.fromM ||
                    section.toM > road.curve.Length() + 0.01f) {
                    errors.push_back("road '" + spec.id + "': invalid centreLineSections range/order/length");
                }
                previousSectionEnd = section.toM;
            }
            road.profile.lanesPerDirection = std::max(1, spec.lanesPerDirection);
            road.profile.laneWidth = spec.laneWidth;
            road.profile.edgeStripWidth = spec.edgeStripWidth;
            road.profile.shoulderWidth = spec.shoulderWidth;
            road.profile.sidewalk = spec.sidewalk;
            road.profile.surface = spec.surface;
            road.profile.crownPercent = spec.surface == Sim::SurfaceType::Gravel || spec.surface == Sim::SurfaceType::Dirt ? 3.0f : 2.0f;
            roads_.push_back(std::move(road));
        }
    }

    void RoadNetwork::BuildIntersections(const MapData& data)
    {
        // Collect approaches per node.
        std::vector<std::vector<Approach>> perNode(data.nodes.size());
        std::vector<std::vector<int>> roadsAtNode(data.nodes.size());
        for (const Road& road : roads_) {
            for (std::size_t k = 0; k < road.nodeIndices.size(); ++k) {
                const int node = road.nodeIndices[k];
                roadsAtNode[static_cast<std::size_t>(node)].push_back(road.index);
                const float nodeS = road.nodeS[k];
                const RoadSample sample = road.curve.Evaluate(nodeS);
                const Vector2 tangent = Normalized(Vector2(sample.tangent.X, sample.tangent.Z));
                if (k > 0) {
                    Approach a;
                    a.road = road.index;
                    a.leavesForward = false;
                    a.nodeS = nodeS;
                    // Direction away from the node towards smaller s: use the previous sample's direction.
                    const RoadSample before = road.curve.Evaluate(std::max(0.0f, nodeS - 1.0f));
                    Vector2 back = Normalized(Vector2(before.position.X - sample.position.X, before.position.Z - sample.position.Z));
                    if (back.LengthSquared() < 0.5f) back = tangent * -1.0f;
                    a.direction = back;
                    a.headingRad = Heading(back);
                    perNode[static_cast<std::size_t>(node)].push_back(a);
                }
                if (k + 1 < road.nodeIndices.size()) {
                    Approach a;
                    a.road = road.index;
                    a.leavesForward = true;
                    a.nodeS = nodeS;
                    const RoadSample after = road.curve.Evaluate(std::min(road.curve.Length(), nodeS + 1.0f));
                    Vector2 fwd = Normalized(Vector2(after.position.X - sample.position.X, after.position.Z - sample.position.Z));
                    if (fwd.LengthSquared() < 0.5f) fwd = tangent;
                    a.direction = fwd;
                    a.headingRad = Heading(fwd);
                    perNode[static_cast<std::size_t>(node)].push_back(a);
                }
            }
        }

        for (std::size_t n = 0; n < data.nodes.size(); ++n) {
            auto& approaches = perNode[n];
            std::vector<int> distinctRoads = roadsAtNode[n];
            std::sort(distinctRoads.begin(), distinctRoads.end());
            distinctRoads.erase(std::unique(distinctRoads.begin(), distinctRoads.end()), distinctRoads.end());
            if (approaches.size() < 2 || distinctRoads.size() < 2) {
                continue;   // dead end or an interior node of a single road
            }
            const RoadNodeSpec& node = data.nodes[n];
            Intersection inter;
            inter.node = static_cast<int>(n);
            inter.center = nodePositions_[n];
            std::sort(approaches.begin(), approaches.end(), [](const Approach& a, const Approach& b) { return a.headingRad < b.headingRad; });

            // Controls.
            const bool passThrough = approaches.size() == 2;
            for (Approach& a : approaches) {
                const std::string& roadId = roads_[static_cast<std::size_t>(a.road)].spec->id;
                const auto override = node.approachControl.find(roadId);
                if (override != node.approachControl.end()) {
                    a.control = override->second;
                } else if (passThrough) {
                    a.control = ApproachControl::Priority;
                } else if (std::find(node.mainRoads.begin(), node.mainRoads.end(), roadId) != node.mainRoads.end()) {
                    a.control = ApproachControl::Priority;
                } else if (!node.mainRoads.empty()) {
                    a.control = ApproachControl::Yield;
                } else {
                    a.control = ApproachControl::RightHandRule;
                }
                inter.hasPriorityRoad = inter.hasPriorityRoad || a.control == ApproachControl::Priority;
            }

            // Signals: the plan comes from the node. Approaches are assigned to the group that
            // names their road; anything the plan does not mention goes into a group of its own
            // after the listed ones, so a half-written plan still runs (it just gives the
            // unnamed approaches their own phase).
            inter.signals = node.signals;
            if (inter.signals.enabled) {
                if (inter.signals.groups.empty()) {
                    // No explicit grouping: the main roads share one phase, the rest the other.
                    std::vector<std::string> main, side;
                    for (const Approach& a : approaches) {
                        const std::string& roadId = roads_[static_cast<std::size_t>(a.road)].spec->id;
                        auto& target = std::find(node.mainRoads.begin(), node.mainRoads.end(), roadId) != node.mainRoads.end() ? main : side;
                        if (std::find(target.begin(), target.end(), roadId) == target.end()) target.push_back(roadId);
                    }
                    if (!main.empty()) inter.signals.groups.push_back(main);
                    if (!side.empty()) inter.signals.groups.push_back(side);
                }
                for (Approach& a : approaches) {
                    const std::string& roadId = roads_[static_cast<std::size_t>(a.road)].spec->id;
                    for (std::size_t g = 0; g < inter.signals.groups.size(); ++g) {
                        const auto& group = inter.signals.groups[g];
                        if (std::find(group.begin(), group.end(), roadId) != group.end()) {
                            a.signalGroup = static_cast<int>(g);
                            break;
                        }
                    }
                    if (a.signalGroup < 0) {
                        inter.signals.groups.push_back({roadId});
                        a.signalGroup = static_cast<int>(inter.signals.groups.size()) - 1;
                    }
                    a.control = ApproachControl::Signal;
                }
            }

            // Setbacks: clear the neighbouring roads' paved edges plus a kerb fillet.
            const float fillet = node.urban ? 5.0f : 8.0f;
            const std::size_t count = approaches.size();
            for (std::size_t i = 0; i < count; ++i) {
                Approach& a = approaches[i];
                const float hwA = roads_[static_cast<std::size_t>(a.road)].profile.HalfPavedWidth();
                float setback = hwA + 1.0f;
                for (std::size_t j = 0; j < count; ++j) {
                    if (i == j) continue;
                    const Approach& b = approaches[j];
                    const float hwB = roads_[static_cast<std::size_t>(b.road)].profile.HalfPavedWidth();
                    const float cosPhi = std::clamp(Vector2::Dot(a.direction, b.direction), -1.0f, 1.0f);
                    const float sinPhi = std::sqrt(std::max(0.0f, 1.0f - cosPhi * cosPhi));
                    const float clear = (hwB + hwA * cosPhi) / std::max(sinPhi, 0.2f);
                    setback = std::max(setback, std::clamp(clear, 0.0f, 40.0f) + fillet + 0.5f);
                }
                a.setback = setback;
            }
            // Surface: the widest road decides.
            float widest = -1.0f;
            for (const Approach& a : approaches) {
                const RoadProfile& p = roads_[static_cast<std::size_t>(a.road)].profile;
                if (p.HalfPavedWidth() > widest) {
                    widest = p.HalfPavedWidth();
                    inter.surface = p.surface;
                }
            }
            inter.approaches = approaches;
            BuildPatch(inter);
            nodeIntersection_[n] = static_cast<int>(intersections_.size());
            intersections_.push_back(std::move(inter));
        }
    }

    void RoadNetwork::BuildPatch(Intersection& inter) const
    {
        const Vector2 c(inter.center.X, inter.center.Z);
        std::vector<Vector2> poly;
        const std::size_t n = inter.approaches.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Approach& a = inter.approaches[i];
            const Approach& b = inter.approaches[(i + 1) % n];
            const float hwA = roads_[static_cast<std::size_t>(a.road)].profile.HalfPavedWidth();
            const float hwB = roads_[static_cast<std::size_t>(b.road)].profile.HalfPavedWidth();
            const Vector2 ea = c + a.direction * a.setback;
            const Vector2 left = ea - Perp(a.direction) * hwA;
            const Vector2 right = ea + Perp(a.direction) * hwA;
            poly.push_back(left);
            poly.push_back(right);
            if (n == 1) {
                break;
            }
            // Fillet between a's right edge and b's left edge.
            const Vector2 eb = c + b.direction * b.setback;
            const Vector2 bLeft = eb - Perp(b.direction) * hwB;
            // Intersect line (right, a.direction) with line (bLeft, b.direction).
            const float denom = a.direction.X * b.direction.Y - a.direction.Y * b.direction.X;
            Vector2 corner = (right + bLeft) * 0.5f;
            if (std::fabs(denom) > 1e-5f) {
                const Vector2 diff = bLeft - right;
                const float t = (diff.X * b.direction.Y - diff.Y * b.direction.X) / denom;
                const Vector2 x = right + a.direction * t;
                // Only accept a corner that lies between the two edge ends (towards the node).
                if (t < 0.0f && t > -(a.setback + 40.0f)) {
                    corner = x;
                }
            }
            // The fillet is a kerb: it rounds the corner between two arms *outward*. Where two
            // arms leave at a sharp angle with very different setbacks, the two edge lines meet on
            // the node side of the chord between them and the quadratic cuts a notch into the
            // paved area. A car turning between those arms then drives over the notch: one wheel
            // off the pavement, and -- because the terrain is flat only inside the patch -- off a
            // step in the ground (0.47 m at Podhájí before this). Keep the fillet on or outside
            // the chord; a straight kerb across the corner is the worst it may become.
            const Vector2 chord = bLeft - right;
            const float chordLength = chord.Length();
            Vector2 outward(0.0f, 0.0f);
            if (chordLength > 1e-5f) {
                outward = Vector2(-chord.Y / chordLength, chord.X / chordLength);
                if (Vector2::Dot(outward, right - c) < 0.0f) {
                    outward = outward * -1.0f;
                }
            }
            for (int k = 1; k < 6; ++k) {
                const float t = static_cast<float>(k) / 6.0f;
                const float u = 1.0f - t;
                Vector2 point = right * (u * u) + corner * (2.0f * u * t) + bLeft * (t * t);
                const float depth = Vector2::Dot(point - right, outward);
                if (depth < 0.0f) {
                    point = point - outward * depth;
                }
                poly.push_back(point);
            }
        }
        if (PolygonArea(poly) < 0.0f) {
            std::reverse(poly.begin(), poly.end());
        }
        inter.patch = std::move(poly);
        inter.radius = 0.0f;
        for (const auto& p : inter.patch) {
            inter.radius = std::max(inter.radius, Vector2::Distance(p, c));
        }
    }

    void RoadNetwork::BuildHeights(const MapData& data, const HeightSampler& terrainHeight)
    {
        // Node heights: explicit elevation or terrain averaged over a small disc.
        std::vector<float> nodeHeight(data.nodes.size(), 0.0f);
        for (std::size_t n = 0; n < data.nodes.size(); ++n) {
            const RoadNodeSpec& node = data.nodes[n];
            if (node.elevation) {
                nodeHeight[n] = *node.elevation;
            } else {
                float sum = 0.0f;
                int count = 0;
                for (int dz = -2; dz <= 2; ++dz) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        sum += terrainHeight(node.position.X + static_cast<float>(dx) * 6.0f, node.position.Y + static_cast<float>(dz) * 6.0f);
                        ++count;
                    }
                }
                nodeHeight[n] = sum / static_cast<float>(count);
            }
            nodePositions_[n].Y = nodeHeight[n];
        }
        for (Intersection& inter : intersections_) {
            inter.height = nodeHeight[static_cast<std::size_t>(inter.node)];
            inter.center.Y = inter.height;
        }

        // Pass 1: filtered terrain profile per road, pinned to the node heights.
        std::vector<std::vector<float>> roadHeights(roads_.size());
        for (Road& road : roads_) {
            const auto& samples = road.curve.Samples();
            const std::size_t count = samples.size();
            if (count == 0) {
                continue;
            }
            std::vector<float> raw(count);
            for (std::size_t i = 0; i < count; ++i) {
                raw[i] = terrainHeight(samples[i].position.X, samples[i].position.Z);
            }
            // Two passes of a moving average (triangle filter) over ~80 m.
            const int half = std::max(1, static_cast<int>(40.0f / sampleSpacing_));
            std::vector<float> filtered = raw;
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> next(count);
                for (std::size_t i = 0; i < count; ++i) {
                    float sum = 0.0f;
                    int k = 0;
                    for (int d = -half; d <= half; ++d) {
                        const long j = static_cast<long>(i) + d;
                        if (j < 0 || j >= static_cast<long>(count)) continue;
                        sum += filtered[static_cast<std::size_t>(j)];
                        ++k;
                    }
                    next[i] = sum / static_cast<float>(std::max(1, k));
                }
                filtered.swap(next);
            }
            // Pin the node heights with a piecewise-linear correction.
            std::vector<float> heights(count);
            const std::size_t nodeCount = road.nodeIndices.size();
            std::vector<float> corr(nodeCount);
            for (std::size_t k = 0; k < nodeCount; ++k) {
                const float target = nodeHeight[static_cast<std::size_t>(road.nodeIndices[k])];
                const float at = road.nodeS[k];
                float f = filtered.back();
                for (std::size_t i = 0; i + 1 < count; ++i) {
                    if (samples[i + 1].s >= at) {
                        const float t = (at - samples[i].s) / std::max(1e-6f, samples[i + 1].s - samples[i].s);
                        f = filtered[i] + (filtered[i + 1] - filtered[i]) * t;
                        break;
                    }
                }
                corr[k] = target - f;
            }
            std::size_t seg = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const float s = samples[i].s;
                while (seg + 2 < nodeCount && s > road.nodeS[seg + 1]) {
                    ++seg;
                }
                const float s0 = road.nodeS[seg];
                const float s1 = road.nodeS[std::min(seg + 1, nodeCount - 1)];
                const float t = s1 > s0 ? std::clamp((s - s0) / (s1 - s0), 0.0f, 1.0f) : 0.0f;
                heights[i] = filtered[i] + corr[seg] + (corr[std::min(seg + 1, nodeCount - 1)] - corr[seg]) * t;
            }
            roadHeights[static_cast<std::size_t>(road.index)] = std::move(heights);
        }

        // Pass 2: a sloped plane per intersection, fitted to the approach gradients so that a
        // through road keeps its grade across the junction (weighted least squares, ridge term).
        const auto heightAt = [&](const Road& road, const float s) {
            const auto& samples = road.curve.Samples();
            const auto& h = roadHeights[static_cast<std::size_t>(road.index)];
            if (samples.size() < 2) return 0.0f;
            const float sc = std::clamp(s, samples.front().s, samples.back().s);
            std::size_t i = 0;
            while (i + 2 < samples.size() && samples[i + 1].s < sc) ++i;
            const float t = (sc - samples[i].s) / std::max(1e-6f, samples[i + 1].s - samples[i].s);
            return h[i] + (h[i + 1] - h[i]) * t;
        };
        for (Intersection& inter : intersections_) {
            float a11 = 0.02f, a12 = 0.0f, a22 = 0.02f, b1 = 0.0f, b2 = 0.0f;   // ridge keeps the plane flat with poor data
            for (const Approach& a : inter.approaches) {
                const Road& road = roads_[static_cast<std::size_t>(a.road)];
                const float probe = std::min(12.0f, a.setback + 6.0f);
                const float sProbe = a.leavesForward ? a.nodeS + probe : a.nodeS - probe;
                const float slope = (heightAt(road, sProbe) - inter.height) / probe;   // along a.direction
                const float w = a.control == ApproachControl::Priority ? 3.0f : 1.0f;
                a11 += w * a.direction.X * a.direction.X;
                a12 += w * a.direction.X * a.direction.Y;
                a22 += w * a.direction.Y * a.direction.Y;
                b1 += w * a.direction.X * slope;
                b2 += w * a.direction.Y * slope;
            }
            const float det = a11 * a22 - a12 * a12;
            if (std::fabs(det) > 1e-9f) {
                inter.gradient = Vector2((b1 * a22 - b2 * a12) / det, (a11 * b2 - a12 * b1) / det);
            }
            const float g = inter.gradient.Length();
            if (g > 0.10f) {
                inter.gradient = inter.gradient * (0.10f / g);   // junctions steeper than 10 % are not built
            }
        }

        // Pass 3: blend every road onto the intersection planes across the setback zone.
        for (Road& road : roads_) {
            auto& heights = roadHeights[static_cast<std::size_t>(road.index)];
            const auto& samples = road.curve.Samples();
            for (const Intersection& inter : intersections_) {
                for (const Approach& a : inter.approaches) {
                    if (a.road != road.index) continue;
                    const float ease = 14.0f;
                    for (std::size_t i = 0; i < samples.size(); ++i) {
                        const float d = a.leavesForward ? samples[i].s - a.nodeS : a.nodeS - samples[i].s;
                        if (d < -0.01f || d > a.setback + ease) continue;
                        const float w = 1.0f - SmoothStep((d - a.setback) / ease);
                        const float plane = inter.PlaneHeight(Vector2(samples[i].position.X, samples[i].position.Z));
                        heights[i] = heights[i] + (plane - heights[i]) * w;
                    }
                }
            }
            road.curve.SetHeights(heights);
        }
    }

    void RoadNetwork::BuildPieces()
    {
        for (Road& road : roads_) {
            struct Cut { float from; float to; int intersection; int approachIndex; bool forward; };
            std::vector<Cut> cuts;
            for (std::size_t ii = 0; ii < intersections_.size(); ++ii) {
                const Intersection& inter = intersections_[ii];
                for (std::size_t ai = 0; ai < inter.approaches.size(); ++ai) {
                    const Approach& a = inter.approaches[ai];
                    if (a.road != road.index) continue;
                    Cut cut;
                    cut.intersection = static_cast<int>(ii);
                    cut.approachIndex = static_cast<int>(ai);
                    cut.forward = a.leavesForward;
                    cut.from = a.leavesForward ? a.nodeS : a.nodeS - a.setback;
                    cut.to = a.leavesForward ? a.nodeS + a.setback : a.nodeS;
                    cuts.push_back(cut);
                }
            }
            std::sort(cuts.begin(), cuts.end(), [](const Cut& x, const Cut& y) { return x.from < y.from; });
            const float length = road.curve.Length();
            float cursor = 0.0f;
            int startIntersection = -1;
            int startApproach = -1;
            int startInterIndex = -1;
            const auto& samples = road.curve.Samples();
            const auto addPiece = [&](const float s0, const float s1, const int endInter, const int endApproachIndex, const int endInterIndex) {
                if (s1 - s0 < 1.0f) {
                    return;
                }
                RoadPiece piece;
                piece.road = road.index;
                piece.s0 = s0;
                piece.s1 = s1;
                piece.startIntersection = startIntersection;
                piece.endIntersection = endInter;
                std::size_t b = 0;
                while (b + 1 < samples.size() && samples[b + 1].s <= s0) ++b;
                std::size_t e = b;
                while (e < samples.size() && samples[e].s < s1) ++e;
                piece.sampleBegin = b;
                piece.sampleEnd = std::min(samples.size(), e + 1);
                const int pieceIndex = static_cast<int>(pieces_.size());
                if (startInterIndex >= 0 && startApproach >= 0) {
                    intersections_[static_cast<std::size_t>(startInterIndex)].approaches[static_cast<std::size_t>(startApproach)].piece = pieceIndex;
                }
                if (endInterIndex >= 0 && endApproachIndex >= 0) {
                    intersections_[static_cast<std::size_t>(endInterIndex)].approaches[static_cast<std::size_t>(endApproachIndex)].piece = pieceIndex;
                }
                road.pieces.push_back(pieceIndex);
                pieces_.push_back(piece);
            };
            for (const Cut& cut : cuts) {
                if (cut.forward) {
                    // Cut begins at the node: the piece before it ends at the backward cut of the same
                    // node (already handled) or at the node itself when the road starts here.
                    if (cut.from > cursor + 0.5f) {
                        addPiece(cursor, cut.from, -1, -1, -1);
                    }
                    cursor = std::max(cursor, cut.to);
                    startIntersection = cut.intersection;
                    startApproach = cut.approachIndex;
                    startInterIndex = cut.intersection;
                } else {
                    addPiece(cursor, cut.from, cut.intersection, cut.approachIndex, cut.intersection);
                    cursor = std::max(cursor, cut.to);
                    startIntersection = -1;
                    startApproach = -1;
                    startInterIndex = -1;
                }
            }
            if (length > cursor + 0.5f) {
                addPiece(cursor, length, -1, -1, -1);
            }
        }
    }

    void RoadNetwork::BuildGrid()
    {
        float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
        for (const Road& road : roads_) {
            for (const auto& s : road.curve.Samples()) {
                minX = std::min(minX, s.position.X); maxX = std::max(maxX, s.position.X);
                minZ = std::min(minZ, s.position.Z); maxZ = std::max(maxZ, s.position.Z);
            }
        }
        if (roads_.empty()) {
            minX = minZ = -1.0f; maxX = maxZ = 1.0f;
        }
        grid_.Reset(minX - 50.0f, minZ - 50.0f, maxX + 50.0f, maxZ + 50.0f, 25.0f);
        intersectionGrid_.Reset(minX - 50.0f, minZ - 50.0f, maxX + 50.0f, maxZ + 50.0f, 25.0f);
        for (const Road& road : roads_) {
            const auto& samples = road.curve.Samples();
            const float pad = road.profile.HalfTotalWidth() + 1.0f;
            for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
                const auto& a = samples[i].position;
                const auto& b = samples[i + 1].position;
                grid_.Insert(road.index * 65536 + static_cast<int>(i), std::min(a.X, b.X) - pad, std::min(a.Z, b.Z) - pad,
                             std::max(a.X, b.X) + pad, std::max(a.Z, b.Z) + pad);
            }
        }
        for (std::size_t i = 0; i < intersections_.size(); ++i) {
            const Intersection& inter = intersections_[i];
            intersectionGrid_.Insert(static_cast<int>(i), inter.center.X - inter.radius, inter.center.Z - inter.radius,
                                     inter.center.X + inter.radius, inter.center.Z + inter.radius);
        }
    }

    int RoadNetwork::IntersectionAtNode(const int nodeIndex) const
    {
        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodeIntersection_.size()) {
            return -1;
        }
        return nodeIntersection_[static_cast<std::size_t>(nodeIndex)];
    }

    float RoadNetwork::TotalLength() const
    {
        float total = 0.0f;
        for (const Road& r : roads_) {
            total += r.curve.Length();
        }
        return total;
    }

    bool RoadNetwork::NearestRoad(const Vector2& point, const float maxLateral, RoadHit& out) const
    {
        bool found = false;
        float bestScore = std::numeric_limits<float>::max();
        grid_.Query(point.X - maxLateral, point.Y - maxLateral, point.X + maxLateral, point.Y + maxLateral, [&](const std::int32_t id) {
            const int roadIndex = id / 65536;
            const std::size_t seg = static_cast<std::size_t>(id % 65536);
            const Road& road = roads_[static_cast<std::size_t>(roadIndex)];
            const auto& samples = road.curve.Samples();
            if (seg + 1 >= samples.size()) return;
            const Vector2 a(samples[seg].position.X, samples[seg].position.Z);
            const Vector2 b(samples[seg + 1].position.X, samples[seg + 1].position.Z);
            float t = 0.0f;
            const float d = DistanceToSegment(point, a, b, t);
            if (d > maxLateral) return;
            const float hp = road.profile.HalfPavedWidth();
            // Points on a paved surface win over closer centrelines of narrower roads.
            const float score = std::max(0.0f, d - hp) * 10.0f + d;
            if (score < bestScore) {
                bestScore = score;
                found = true;
                out.road = roadIndex;
                out.s = samples[seg].s + (samples[seg + 1].s - samples[seg].s) * t;
                const Vector2 dir = Normalized(b - a);
                const Vector2 proj = a + (b - a) * t;
                out.lateral = Vector2::Dot(point - proj, Perp(dir));
                out.distance = d;
            }
        });
        if (found) {
            out.sample = roads_[static_cast<std::size_t>(out.road)].curve.Evaluate(out.s);
        }
        return found;
    }

    int RoadNetwork::IntersectionContaining(const Vector2& point) const
    {
        int result = -1;
        intersectionGrid_.Query(point.X, point.Y, point.X, point.Y, [&](const std::int32_t id) {
            if (result >= 0) return;
            const Intersection& inter = intersections_[static_cast<std::size_t>(id)];
            if (PointInPolygon(point, inter.patch)) {
                result = id;
            }
        });
        return result;
    }

    float RoadNetwork::CrownScale(const Road& road, const float s) const
    {
        constexpr float kEase = 14.0f;   // the same run BuildHeights uses to ease onto the plane
        float scale = 1.0f;
        for (const auto& j : road.junctions) {
            const float d = std::fabs(s - j.nodeS);
            if (d >= j.setback + kEase) continue;
            scale = std::min(scale, SmoothStep((d - j.setback) / kEase));
        }
        return scale;
    }

    float RoadNetwork::SurfaceHeightAt(const RoadHit& hit, const Vector2& point) const
    {
        constexpr float kEase = 14.0f;
        const Road& road = roads_[static_cast<std::size_t>(hit.road)];
        float y = SurfaceHeight(hit);
        for (const auto& j : road.junctions) {
            if (j.intersection < 0) continue;
            const float d = std::fabs(hit.s - j.nodeS);
            if (d >= j.setback + kEase) continue;
            const float w = 1.0f - SmoothStep((d - j.setback) / kEase);
            y += (intersections_[static_cast<std::size_t>(j.intersection)].PlaneHeight(point) - y) * w;
        }
        return y;
    }

    float RoadNetwork::SurfaceHeight(const RoadHit& hit) const
    {
        const Road& road = roads_[static_cast<std::size_t>(hit.road)];
        const float hp = road.profile.HalfPavedWidth();
        const float lat = std::fabs(hit.lateral);
        // The junction apron is flat, so the crossfall has to be gone by the time the carriageway
        // reaches it; otherwise the terrain steps by the crossfall where the two meet, which is a
        // ridge a car drives over at every junction.
        const float crown = road.profile.crownPercent * 0.01f * CrownScale(road, hit.s);
        const float shoulder = 0.04f * CrownScale(road, hit.s);
        float y = hit.sample.position.Y;
        if (lat <= hp) {
            y -= lat * crown;
        } else {
            y -= hp * crown + (lat - hp) * shoulder;   // shoulder falls away a little faster
        }
        return y;
    }

    bool RoadNetwork::RoadSurfaceAt(const Vector2& point, float& height, Sim::SurfaceType& surface, float& distanceToPavedEdge) const
    {
        const int inter = IntersectionContaining(point);
        if (inter >= 0) {
            const Intersection& x = intersections_[static_cast<std::size_t>(inter)];
            height = x.PlaneHeight(point);
            surface = x.surface;
            float edge = std::numeric_limits<float>::max();
            for (std::size_t i = 0, j = x.patch.size() - 1; i < x.patch.size(); j = i++) {
                float t = 0.0f;
                edge = std::min(edge, DistanceToSegment(point, x.patch[j], x.patch[i], t));
            }
            distanceToPavedEdge = -edge;
            return true;
        }
        RoadHit hit;
        if (!NearestRoad(point, 14.0f, hit)) {
            return false;
        }
        const Road& road = roads_[static_cast<std::size_t>(hit.road)];
        const float hp = road.profile.HalfPavedWidth();
        const float ht = road.profile.HalfTotalWidth();
        const float lat = std::fabs(hit.lateral);
        if (lat > ht) {
            return false;
        }
        height = SurfaceHeightAt(hit, point);
        distanceToPavedEdge = lat - hp;
        if (lat <= hp) {
            surface = road.profile.surface;
        } else {
            const bool right = hit.lateral > 0.0f;
            if (hit.sample.urban && road.profile.SidewalkOuter(right) > 0.0f) {
                height += road.profile.sidewalk.kerbHeight;
                surface = Sim::SurfaceType::Concrete;
            } else {
                surface = Sim::SurfaceType::Gravel;
            }
        }
        return true;
    }
}
