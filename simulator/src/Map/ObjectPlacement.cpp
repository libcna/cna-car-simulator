#include "CarSim/Map/ObjectPlacement.hpp"

#include "CarSim/Core/Noise.hpp"
#include "CarSim/Map/MapWorld.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace CarSim::Map
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        constexpr float kPi = std::numbers::pi_v<float>;

        float DistanceToPolygonEdge(const Vector2& p, const std::vector<Vector2>& polygon)
        {
            float best = std::numeric_limits<float>::max();
            for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
                float t = 0.0f;
                best = std::min(best, DistanceToSegment(p, polygon[j], polygon[i], t));
            }
            return best;
        }

        float Hash01(const int x, const int y, const unsigned seed) { return Core::Noise::Hash(x, y, seed); }
    }

    bool ParseTreeSpecies(const std::string& text, TreeSpecies& out)
    {
        if (text == "linden" || text == "lime") out = TreeSpecies::Linden;
        else if (text == "oak") out = TreeSpecies::Oak;
        else if (text == "birch") out = TreeSpecies::Birch;
        else if (text == "maple") out = TreeSpecies::Maple;
        else if (text == "beech") out = TreeSpecies::Beech;
        else if (text == "spruce") out = TreeSpecies::Spruce;
        else if (text == "pine") out = TreeSpecies::Pine;
        else if (text == "bush" || text == "shrub") out = TreeSpecies::Bush;
        else return false;
        return true;
    }

    const char* ToString(const TreeSpecies s)
    {
        switch (s) {
            case TreeSpecies::Linden: return "linden";
            case TreeSpecies::Oak: return "oak";
            case TreeSpecies::Birch: return "birch";
            case TreeSpecies::Maple: return "maple";
            case TreeSpecies::Beech: return "beech";
            case TreeSpecies::Spruce: return "spruce";
            case TreeSpecies::Pine: return "pine";
            case TreeSpecies::Bush: return "bush";
        }
        return "?";
    }

    bool IsConifer(const TreeSpecies s) { return s == TreeSpecies::Spruce || s == TreeSpecies::Pine; }

    float PlacedTree::Height() const
    {
        switch (species) {
            case TreeSpecies::Linden: return 14.0f * scale;
            case TreeSpecies::Oak: return 15.0f * scale;
            case TreeSpecies::Birch: return 13.0f * scale;
            case TreeSpecies::Maple: return 12.0f * scale;
            case TreeSpecies::Beech: return 18.0f * scale;
            case TreeSpecies::Spruce: return 22.0f * scale;
            case TreeSpecies::Pine: return 19.0f * scale;
            case TreeSpecies::Bush: return 2.2f * scale;
        }
        return 12.0f * scale;
    }

    float PlacedTree::CrownRadius() const
    {
        switch (species) {
            case TreeSpecies::Spruce: return 2.6f * scale;
            case TreeSpecies::Pine: return 3.2f * scale;
            case TreeSpecies::Birch: return 3.0f * scale;
            case TreeSpecies::Bush: return 1.3f * scale;
            default: return 4.5f * scale;
        }
    }

    float PlacedTree::TrunkRadius() const
    {
        if (species == TreeSpecies::Bush) return 0.05f * scale;
        return (IsConifer(species) ? 0.22f : 0.28f) * scale;
    }

    bool ParsePropType(const std::string& text, PropType& out)
    {
        if (text == "bus_stop") out = PropType::BusStop;
        else if (text == "bench") out = PropType::Bench;
        else if (text == "lamp") out = PropType::Lamp;
        else if (text == "fence") out = PropType::Fence;
        else if (text == "wall") out = PropType::Wall;
        else if (text == "gate") out = PropType::Gate;
        else if (text == "timber_stack") out = PropType::TimberStack;
        else if (text == "hydrant") out = PropType::Hydrant;
        else if (text == "bin") out = PropType::Bin;
        else if (text == "delineator") out = PropType::Delineator;
        else if (text == "memorial") out = PropType::Memorial;
        else if (text == "wire_fence") out = PropType::WireFence;
        else if (text == "hedge") out = PropType::Hedge;
        else if (text == "shed") out = PropType::Shed;
        else if (text == "utility_pole") out = PropType::UtilityPole;
        else return false;
        return true;
    }

    // ------------------------------------------------------------------ build

    void ObjectPlacement::Build(const MapWorld& world, std::vector<std::string>& warnings)
    {
        buildings_.clear();
        trees_.clear();
        signs_.clear();
        props_.clear();
        vehicles_.clear();
        PlaceBuildings(world);
        PlaceTrees(world, warnings);
        PlaceAvenues(world, warnings);
        PlaceSigns(world);
        PlaceProps(world, warnings);
        PlaceVehicles(world, warnings);
        PlaceDelineators(world);
        PlacePlots(world);
        PlaceUtilityPoles(world);
        PlaceBushes(world);
        BuildGrids(world);
    }

    void ObjectPlacement::PlaceBushes(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        const std::size_t treeCount = trees_.size();   // trees placed so far (authored, forests, avenues)
        const auto nearTree = [&](const Vector2& p, const float radius) {
            for (std::size_t i = 0; i < treeCount; ++i) {
                const PlacedTree& t = trees_[i];
                if (std::fabs(t.position.X - p.X) > radius || std::fabs(t.position.Z - p.Y) > radius) continue;
                if (Vector2::DistanceSquared(Vector2(t.position.X, t.position.Z), p) < radius * radius) return true;
            }
            return false;
        };
        const auto add = [&](const Vector2& p, const float scale, const unsigned seed) {
            if (!world.Terrain().Contains(p.X, p.Y) || InsideBuilding(p, 2.0f) || !ClearOfRoads(world, p, 1.2f) || nearTree(p, 3.5f)) return;
            PlacedTree b;
            b.species = TreeSpecies::Bush;
            b.scale = scale;
            b.seed = seed;
            b.rotationRad = Hash01(static_cast<int>(seed), 3, 77u) * 2.0f * kPi;
            b.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
            trees_.push_back(b);
        };
        // Rural verges: a shrub every ~9 m on either side with a 45 % chance, 3-5.5 m off the road.
        int n = 0;
        for (const Road& road : network.Roads()) {
            for (float s = 6.0f; s < road.curve.Length() - 6.0f; s += 9.0f, ++n) {
                const RoadSample sample = road.curve.Evaluate(s);
                if (sample.urban) continue;
                Vector2 tangent(sample.tangent.X, sample.tangent.Z);
                if (tangent.LengthSquared() < 1e-6f) continue;
                tangent.Normalize();
                const Vector2 right(-tangent.Y, tangent.X);
                const Vector2 centre(sample.position.X, sample.position.Z);
                for (const int side : {-1, 1}) {
                    if (Hash01(n, side + 2, 501u + static_cast<unsigned>(road.index)) > 0.45f) continue;
                    const float lateral = road.profile.HalfTotalWidth() + 3.0f + 2.5f * Hash01(n, side + 5, 503u);
                    const float along = (Hash01(n, side + 8, 505u) - 0.5f) * 4.0f;
                    const Vector2 p = centre + right * (static_cast<float>(side) * lateral) + tangent * along;
                    add(p, 0.7f + 0.8f * Hash01(n, side + 11, 507u), 900u + static_cast<unsigned>(n * 2 + (side > 0 ? 1 : 0)));
                }
            }
        }
        // Forest edges: shrubs just outside the polygon every ~7 m with a 60 % chance.
        unsigned forestIndex = 0;
        for (const ForestSpec& forest : world.Data().objects.forests) {
            ++forestIndex;
            const std::size_t count = forest.polygon.size();
            if (count < 3) continue;
            int k = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const Vector2& a = forest.polygon[i];
                const Vector2& b = forest.polygon[(i + 1) % count];
                const float length = Vector2::Distance(a, b);
                if (length < 1.0f) continue;
                const Vector2 dir = (b - a) * (1.0f / length);
                Vector2 normal(-dir.Y, dir.X);
                if (PointInPolygon(a + dir * (length * 0.5f) + normal * 3.0f, forest.polygon)) normal = normal * -1.0f;   // point outward
                for (float t = 3.5f; t < length - 3.5f; t += 7.0f, ++k) {
                    if (Hash01(k, static_cast<int>(i), 600u + forest.seed + forestIndex) > 0.6f) continue;
                    const float out = 2.0f + 2.5f * Hash01(k, static_cast<int>(i) + 3, 602u + forest.seed);
                    const Vector2 p = a + dir * (t + (Hash01(k, static_cast<int>(i) + 6, 604u) - 0.5f) * 3.0f) + normal * out;
                    add(p, 0.8f + 0.9f * Hash01(k, static_cast<int>(i) + 9, 606u), 40000u + forestIndex * 1000u + static_cast<unsigned>(k));
                }
            }
        }
    }

    void ObjectPlacement::PlacePlots(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        const auto clear = [&](const Vector2& p, const float margin) {
            return world.Terrain().Contains(p.X, p.Y) && !InsideBuilding(p, margin) && ClearOfRoads(world, p, margin);
        };
        const auto addLine = [&](const PropType type, const Vector2& a, const Vector2& b, const Vector2& facing) {
            const float length = Vector2::Distance(a, b);
            if (length < 1.6f) return;
            const Vector2 mid = (a + b) * 0.5f;
            for (const Vector2& q : {a, mid, b}) {
                if (!clear(q, 0.3f)) return;
            }
            PlacedProp f;
            f.type = type;
            f.length = length;
            f.position = Vector3(mid.X, ground.HeightAt(mid.X, mid.Y), mid.Y);
            f.headingRad = HeadingFromDirection(facing.X, facing.Y);
            props_.push_back(f);
        };
        for (const PlacedBuilding& b : buildings_) {
            const std::string& type = b.spec->type;
            if (type != "house" && type != "cottage") continue;
            const unsigned seed = b.spec->seed;
            const Vector2 centre(b.position.X, b.position.Z);
            RoadHit hit;
            if (!network.NearestRoad(centre, 45.0f, hit)) continue;
            const Road& road = network.Roads()[static_cast<std::size_t>(hit.road)];
            const float edge = road.profile.HalfTotalWidth();
            const float gap = std::fabs(hit.lateral) - edge;        // road edge to the building centre
            const float reach = std::max(b.halfWidth, b.halfDepth);
            if (gap < reach + 1.5f) continue;                      // no front garden to fence
            Vector2 tangent(hit.sample.tangent.X, hit.sample.tangent.Z);
            if (tangent.LengthSquared() < 1e-6f) continue;
            tangent.Normalize();
            const Vector2 right(-tangent.Y, tangent.X);
            const float side = hit.lateral > 0.0f ? 1.0f : -1.0f;
            const Vector2 away = right * side;                       // from the road towards the plot
            const Vector2 roadPoint(hit.sample.position.X, hit.sample.position.Z);
            const Vector2 lineCentre = roadPoint + away * (edge + 0.6f);
            const float halfExtent = reach + 2.5f;
            const float gateAt = 1.2f, gateHalf = 0.8f;            // gate opening a little right of centre
            const PropType kind = seed % 3u == 0u ? PropType::Fence : (seed % 3u == 1u ? PropType::WireFence : PropType::Hedge);
            addLine(kind, lineCentre + tangent * -halfExtent, lineCentre + tangent * (gateAt - gateHalf), away * -1.0f);
            addLine(kind, lineCentre + tangent * (gateAt + gateHalf), lineCentre + tangent * halfExtent, away * -1.0f);
            if (type == "cottage") {
                // Side fences from the street line back past the house.
                const float depth = gap + reach + 2.0f;
                for (const float sx : {-1.0f, 1.0f}) {
                    const Vector2 a = lineCentre + tangent * (sx * halfExtent);
                    addLine(kind, a, a + away * depth, tangent * -sx);
                }
            }
            if (seed % 2u == 0u) {
                const Vector2 shedAt = centre + away * (reach + 3.2f) + tangent * (reach - 1.2f);
                if (clear(shedAt, 1.6f)) {
                    PlacedProp shed;
                    shed.type = PropType::Shed;
                    shed.position = Vector3(shedAt.X, ground.HeightAt(shedAt.X, shedAt.Y), shedAt.Y);
                    shed.headingRad = HeadingFromDirection(-away.X, -away.Y);   // door towards the house
                    props_.push_back(shed);
                }
            }
        }
    }

    void ObjectPlacement::PlaceUtilityPoles(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        for (const Road& road : network.Roads()) {
            const RoadClass cls = road.spec->roadClass;
            if (cls != RoadClass::ClassIII && cls != RoadClass::Local && cls != RoadClass::Residential) continue;
            for (float s = 21.0f; s < road.curve.Length() - 8.0f; s += 42.0f) {
                const RoadSample sample = road.curve.Evaluate(s);
                bool nearJunction = false;
                for (const Intersection& inter : network.Intersections()) {
                    for (const Approach& a : inter.approaches) {
                        if (a.road == road.index && std::fabs(a.nodeS - s) < a.setback + 6.0f) nearJunction = true;
                    }
                }
                if (nearJunction) continue;
                const Vector2 centre(sample.position.X, sample.position.Z);
                Vector2 tangent(sample.tangent.X, sample.tangent.Z);
                if (tangent.LengthSquared() < 1e-6f) continue;
                tangent.Normalize();
                const Vector2 right(-tangent.Y, tangent.X);
                const float lateral = road.profile.HalfTotalWidth() + (sample.urban ? 0.5f : 2.0f);
                const Vector2 p = centre - right * lateral;   // left side, opposite the delineator rhythm
                if (!world.Terrain().Contains(p.X, p.Y) || InsideBuilding(p, 1.2f)) continue;
                PlacedProp pole;
                pole.type = PropType::UtilityPole;
                pole.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                pole.headingRad = HeadingFromDirection(tangent.X, tangent.Y);   // crossarm across the line direction
                props_.push_back(pole);
            }
        }
    }

    bool ObjectPlacement::ClearOfRoads(const MapWorld& world, const Vector2& p, const float margin) const
    {
        RoadHit hit;
        if (!world.Roads().NearestRoad(p, margin + 12.0f, hit)) {
            return world.Roads().IntersectionContaining(p) < 0;
        }
        const Road& road = world.Roads().Roads()[static_cast<std::size_t>(hit.road)];
        if (std::fabs(hit.lateral) - road.profile.HalfTotalWidth() < margin) {
            return false;
        }
        for (const Intersection& inter : world.Roads().Intersections()) {
            if (Vector2::Distance(p, Vector2(inter.center.X, inter.center.Z)) < inter.radius + margin) {
                return false;
            }
        }
        return true;
    }

    void ObjectPlacement::PlaceBuildings(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        for (const BuildingSpec& spec : world.Data().objects.buildings) {
            PlacedBuilding b;
            b.spec = &spec;
            b.headingRad = spec.rotationDeg * kPi / 180.0f;
            b.halfWidth = spec.width * 0.5f;
            b.halfDepth = spec.depth * 0.5f;
            b.height = spec.eavesHeight;
            const float pitch = spec.roofPitchDeg * kPi / 180.0f;
            b.roofHeight = std::tan(pitch) * std::min(b.halfDepth, b.halfWidth);
            // Foundation: highest corner defines the floor, walls extend down to the lowest corner.
            const Vector2 fwd = DirectionFromHeading(b.headingRad);
            const Vector2 right(-fwd.Y, fwd.X);
            float lowest = std::numeric_limits<float>::max();
            float highest = -lowest;
            for (const float sx : {-1.0f, 1.0f}) {
                for (const float sz : {-1.0f, 1.0f}) {
                    const Vector2 corner = spec.position + right * (sx * b.halfWidth) + fwd * (sz * b.halfDepth);
                    const float h = ground.HeightAt(corner.X, corner.Y);
                    lowest = std::min(lowest, h);
                    highest = std::max(highest, h);
                }
            }
            const float centre = ground.HeightAt(spec.position.X, spec.position.Y);
            const float floor = std::max(centre, highest - 0.35f);
            b.position = Vector3(spec.position.X, floor + 0.05f, spec.position.Y);
            b.foundationDrop = std::max(0.3f, floor + 0.05f - lowest + 0.3f);
            buildings_.push_back(b);
        }
    }

    bool ObjectPlacement::InsideBuilding(const Vector2& point, const float margin) const
    {
        for (const PlacedBuilding& b : buildings_) {
            const Vector2 c(b.position.X, b.position.Z);
            const float reach = std::hypot(b.halfWidth, b.halfDepth) + margin;
            if (Vector2::DistanceSquared(c, point) > reach * reach) {
                continue;
            }
            const Vector2 fwd = DirectionFromHeading(b.headingRad);
            const Vector2 right(-fwd.Y, fwd.X);
            const Vector2 d = point - c;
            if (std::fabs(Vector2::Dot(d, right)) <= b.halfWidth + margin && std::fabs(Vector2::Dot(d, fwd)) <= b.halfDepth + margin) {
                return true;
            }
        }
        return false;
    }

    void ObjectPlacement::PlaceTrees(const MapWorld& world, std::vector<std::string>& warnings)
    {
        const MapGround& ground = world.Ground();
        // Individually authored trees.
        for (const TreeSpec& spec : world.Data().objects.trees) {
            PlacedTree t;
            if (!ParseTreeSpecies(spec.species, t.species)) {
                warnings.push_back("objects.trees: unknown species '" + spec.species + "', using linden");
            }
            t.position = Vector3(spec.position.X, ground.HeightAt(spec.position.X, spec.position.Y), spec.position.Y);
            t.scale = spec.scale;
            t.seed = spec.seed;
            t.rotationRad = Hash01(static_cast<int>(spec.seed), 3, 11u) * 2.0f * kPi;
            trees_.push_back(t);
        }
        // Forests: jittered grid sampling inside the polygon.
        std::size_t forestIndex = 0;
        for (const ForestSpec& forest : world.Data().objects.forests) {
            ++forestIndex;
            if (forest.polygon.size() < 3 || forest.density <= 0.0f) {
                continue;
            }
            std::vector<std::pair<TreeSpecies, float>> species;
            float totalWeight = 0.0f;
            for (const auto& [name, weight] : forest.species) {
                TreeSpecies s;
                if (!ParseTreeSpecies(name, s)) {
                    warnings.push_back("objects.forests[" + std::to_string(forestIndex - 1) + "]: unknown species '" + name + "'");
                    continue;
                }
                species.emplace_back(s, weight);
                totalWeight += weight;
            }
            if (species.empty()) {
                species.emplace_back(TreeSpecies::Spruce, 1.0f);
                totalWeight = 1.0f;
            }
            float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
            for (const auto& p : forest.polygon) {
                minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
                minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
            }
            const float cell = std::sqrt(1.0f / forest.density);
            const int cols = static_cast<int>((maxX - minX) / cell) + 1;
            const int rows = static_cast<int>((maxZ - minZ) / cell) + 1;
            std::size_t placed = 0;
            for (int row = 0; row < rows; ++row) {
                for (int col = 0; col < cols; ++col) {
                    const float jx = Hash01(col, row, forest.seed) * 0.9f + 0.05f;
                    const float jz = Hash01(col, row, forest.seed + 17u) * 0.9f + 0.05f;
                    const Vector2 p(minX + (static_cast<float>(col) + jx) * cell, minZ + (static_cast<float>(row) + jz) * cell);
                    if (!PointInPolygon(p, forest.polygon)) continue;
                    if (DistanceToPolygonEdge(p, forest.polygon) < forest.margin * 0.5f) continue;
                    if (!ClearOfRoads(world, p, forest.margin)) continue;
                    if (InsideBuilding(p, 4.0f)) continue;
                    if (!world.Terrain().Contains(p.X, p.Y)) continue;
                    // Thin out with low-frequency noise so the forest has clearings and dense groups.
                    const float density = Core::Noise::FbmSigned(p.X * 0.004f, p.Y * 0.004f, 2, 0.5f, forest.seed + 5u);
                    if (Hash01(col, row, forest.seed + 29u) < 0.25f - 0.35f * density) continue;
                    PlacedTree t;
                    float pick = Hash01(col, row, forest.seed + 41u) * totalWeight;
                    t.species = species.back().first;
                    for (const auto& [s, w] : species) {
                        pick -= w;
                        if (pick <= 0.0f) { t.species = s; break; }
                    }
                    t.scale = 0.75f + 0.5f * Hash01(col, row, forest.seed + 53u);
                    t.rotationRad = Hash01(col, row, forest.seed + 67u) * 2.0f * kPi;
                    t.seed = static_cast<unsigned>(col * 7919 + row * 104729) + forest.seed;
                    t.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                    trees_.push_back(t);
                    ++placed;
                }
            }
            if (placed == 0) {
                warnings.push_back("objects.forests[" + std::to_string(forestIndex - 1) + "]: no tree could be placed");
            }
        }
    }

    void ObjectPlacement::PlaceAvenues(const MapWorld& world, std::vector<std::string>& warnings)
    {
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        std::size_t index = 0;
        for (const AvenueSpec& avenue : world.Data().objects.avenues) {
            ++index;
            const Road* road = nullptr;
            for (const Road& r : network.Roads()) {
                if (r.spec->id == avenue.road) road = &r;
            }
            if (!road) {
                warnings.push_back("objects.avenues[" + std::to_string(index - 1) + "]: unknown road '" + avenue.road + "'");
                continue;
            }
            TreeSpecies species = TreeSpecies::Linden;
            if (!ParseTreeSpecies(avenue.species, species)) {
                warnings.push_back("objects.avenues[" + std::to_string(index - 1) + "]: unknown species '" + avenue.species + "'");
            }
            float s0 = 0.0f;
            float s1 = road->curve.Length();
            const auto nodeS = [&](const std::string& id, float& out) {
                for (std::size_t k = 0; k < road->nodeIndices.size(); ++k) {
                    if (world.Data().nodes[static_cast<std::size_t>(road->nodeIndices[k])].id == id) {
                        out = road->nodeS[k];
                        return true;
                    }
                }
                return false;
            };
            if (!avenue.fromNode.empty() && !nodeS(avenue.fromNode, s0)) {
                warnings.push_back("objects.avenues[" + std::to_string(index - 1) + "]: fromNode '" + avenue.fromNode + "' is not on the road");
            }
            if (!avenue.toNode.empty() && !nodeS(avenue.toNode, s1)) {
                warnings.push_back("objects.avenues[" + std::to_string(index - 1) + "]: toNode '" + avenue.toNode + "' is not on the road");
            }
            if (s1 < s0) std::swap(s0, s1);
            const float lateral = road->profile.HalfTotalWidth() + avenue.offset;
            int n = 0;
            for (float s = s0 + avenue.spacing * 0.5f; s < s1; s += avenue.spacing, ++n) {
                const RoadSample sample = road->curve.Evaluate(s);
                // Keep clear of junctions.
                bool nearJunction = false;
                for (const Intersection& inter : network.Intersections()) {
                    for (const Approach& a : inter.approaches) {
                        if (a.road == road->index && std::fabs(a.nodeS - s) < a.setback + 12.0f) nearJunction = true;
                    }
                }
                if (nearJunction) continue;
                const Vector2 centre(sample.position.X, sample.position.Z);
                const Vector2 right(-sample.tangent.Z, sample.tangent.X);
                for (const float side : {-1.0f, 1.0f}) {
                    if ((side < 0.0f && !avenue.left) || (side > 0.0f && !avenue.right)) continue;
                    const Vector2 p = centre + right * (side * lateral);
                    if (InsideBuilding(p, 2.0f) || !world.Terrain().Contains(p.X, p.Y)) continue;
                    PlacedTree t;
                    t.species = species;
                    t.scale = 0.85f + 0.3f * Hash01(n, side > 0.0f ? 1 : 0, avenue.seed);
                    t.rotationRad = Hash01(n, 7, avenue.seed) * 2.0f * kPi;
                    t.seed = avenue.seed * 131u + static_cast<unsigned>(n * 2 + (side > 0.0f ? 1 : 0));
                    t.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                    trees_.push_back(t);
                }
            }
        }
    }

    void ObjectPlacement::PlaceSigns(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        for (const SignSpec& spec : world.Data().objects.signs) {
            PlacedSign s;
            s.spec = &spec;
            s.headingRad = spec.headingDeg * kPi / 180.0f;
            s.position = Vector3(spec.position.X, ground.HeightAt(spec.position.X, spec.position.Y), spec.position.Y);
            RoadHit hit;
            s.urban = world.Roads().NearestRoad(spec.position, 25.0f, hit) && hit.sample.urban;
            signs_.push_back(s);
        }
    }

    void ObjectPlacement::PlaceProps(const MapWorld& world, std::vector<std::string>& warnings)
    {
        const MapGround& ground = world.Ground();
        std::size_t index = 0;
        for (const PropSpec& spec : world.Data().objects.props) {
            ++index;
            PlacedProp p;
            if (!ParsePropType(spec.type, p.type)) {
                warnings.push_back("objects.props[" + std::to_string(index - 1) + "]: unknown prop type '" + spec.type + "'");
                continue;
            }
            p.headingRad = spec.rotationDeg * kPi / 180.0f;
            p.length = spec.length;
            p.scale = spec.scale > 0.0f ? spec.scale : 1.0f;
            p.position = Vector3(spec.position.X, ground.HeightAt(spec.position.X, spec.position.Y), spec.position.Y);
            props_.push_back(p);
        }
    }

    void ObjectPlacement::PlaceVehicles(const MapWorld& world, std::vector<std::string>& warnings)
    {
        const MapGround& ground = world.Ground();
        std::size_t index = 0;
        for (const VehicleSpec& spec : world.Data().objects.vehicles) {
            ++index;
            PlacedVehicle v;
            if (!Sim::CarStyle::ParseBody(spec.body, v.body)) {
                warnings.push_back("objects.vehicles[" + std::to_string(index - 1) + "]: unknown body '" + spec.body + "'");
                continue;
            }
            v.headingRad = spec.rotationDeg * kPi / 180.0f;
            v.seed = spec.seed;
            v.position = Vector3(spec.position.X, ground.HeightAt(spec.position.X, spec.position.Y), spec.position.Y);
            vehicles_.push_back(v);
        }
    }

    void ObjectPlacement::PlaceDelineators(const MapWorld& world)
    {
        // Z 11a/b posts every 50 m on rural stretches of class I-III roads, both sides.
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        for (const Road& road : network.Roads()) {
            const RoadClass cls = road.spec->roadClass;
            if (cls != RoadClass::ClassI && cls != RoadClass::ClassII && cls != RoadClass::ClassIII) {
                continue;
            }
            const float lateral = road.profile.HalfPavedWidth() + road.profile.shoulderWidth + 0.35f;
            for (float s = 25.0f; s < road.curve.Length() - 10.0f; s += 50.0f) {
                const RoadSample sample = road.curve.Evaluate(s);
                if (sample.urban) continue;
                bool nearJunction = false;
                for (const Intersection& inter : network.Intersections()) {
                    for (const Approach& a : inter.approaches) {
                        if (a.road == road.index && std::fabs(a.nodeS - s) < a.setback + 8.0f) nearJunction = true;
                    }
                }
                if (nearJunction) continue;
                const Vector2 centre(sample.position.X, sample.position.Z);
                const Vector2 right(-sample.tangent.Z, sample.tangent.X);
                for (const float side : {-1.0f, 1.0f}) {
                    const Vector2 p = centre + right * (side * lateral);
                    PlacedProp post;
                    post.type = PropType::Delineator;
                    post.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                    // The post's reflector face points against the traffic on its side of the road:
                    // right side (+s traffic) faces -s; left side faces +s.
                    post.headingRad = HeadingFromDirection(-side * sample.tangent.X, -side * sample.tangent.Z);
                    post.reflectorRight = true;
                    props_.push_back(post);
                }
            }
        }
    }

    void ObjectPlacement::BuildGrids(const MapWorld& world)
    {
        const TerrainField& t = world.Terrain();
        buildingGrid_.Reset(t.MinX(), t.MinZ(), t.MaxX(), t.MaxZ(), 50.0f);
        treeGrid_.Reset(t.MinX(), t.MinZ(), t.MaxX(), t.MaxZ(), 50.0f);
        for (std::size_t i = 0; i < buildings_.size(); ++i) {
            const PlacedBuilding& b = buildings_[i];
            const float reach = std::hypot(b.halfWidth, b.halfDepth);
            buildingGrid_.Insert(static_cast<std::int32_t>(i), b.position.X - reach, b.position.Z - reach, b.position.X + reach, b.position.Z + reach);
        }
        for (std::size_t i = 0; i < trees_.size(); ++i) {
            const PlacedTree& tr = trees_[i];
            const float r = tr.CrownRadius();
            treeGrid_.Insert(static_cast<std::int32_t>(i), tr.position.X - r, tr.position.Z - r, tr.position.X + r, tr.position.Z + r);
        }
    }
}
