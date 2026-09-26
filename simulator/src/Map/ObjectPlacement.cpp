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

        bool BuildingRoadClear(const MapWorld& world, const PlacedBuilding& building, const Vector2& centre)
        {
            // Check the whole rotated footprint, including the gaps between its corners. A
            // winding road can pass through a long building without touching any corner.
            const Vector2 fwd = DirectionFromHeading(building.headingRad);
            const Vector2 right(-fwd.Y, fwd.X);
            const int widthSteps = std::max(1, static_cast<int>(std::ceil(building.halfWidth * 2.0f)));
            const int depthSteps = std::max(1, static_cast<int>(std::ceil(building.halfDepth * 2.0f)));
            for (int z = 0; z <= depthSteps; ++z) {
                for (int x = 0; x <= widthSteps; ++x) {
                    const float across = -building.halfWidth + 2.0f * building.halfWidth * static_cast<float>(x) / widthSteps;
                    const float along = -building.halfDepth + 2.0f * building.halfDepth * static_cast<float>(z) / depthSteps;
                    const Vector2 point = centre + right * across + fwd * along;
                    if (!world.Terrain().Contains(point.X, point.Y)) return false;
                    float height = 0.0f;
                    Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
                    float pavedEdge = 0.0f;
                    if (world.Roads().RoadSurfaceAt(point, height, surface, pavedEdge) && pavedEdge < 0.75f) return false;
                }
            }
            return true;
        }

        bool BuildingFootprintsOverlap(const PlacedBuilding& a, const Vector2& aCentre,
                                       const PlacedBuilding& b, const Vector2& bCentre)
        {
            const Vector2 af = DirectionFromHeading(a.headingRad), ar(-af.Y, af.X);
            const Vector2 bf = DirectionFromHeading(b.headingRad), br(-bf.Y, bf.X);
            const Vector2 delta = bCentre - aCentre;
            for (const Vector2& axis : {af, ar, bf, br}) {
                const float aRadius = a.halfWidth * std::fabs(Vector2::Dot(axis, ar)) +
                                      a.halfDepth * std::fabs(Vector2::Dot(axis, af));
                const float bRadius = b.halfWidth * std::fabs(Vector2::Dot(axis, br)) +
                                      b.halfDepth * std::fabs(Vector2::Dot(axis, bf));
                if (std::fabs(Vector2::Dot(delta, axis)) >= aRadius + bRadius + 0.5f) return false;
            }
            return true;
        }
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

    float PlacedBuilding::PassageHalfWidth() const
    {
        return spec && spec->type == "castle_gate" ? std::min(2.3f, halfWidth * 0.45f) : 0.0f;
    }

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
        else if (text == "planter") out = PropType::Planter;
        else if (text == "delineator") out = PropType::Delineator;
        else if (text == "memorial") out = PropType::Memorial;
        else if (text == "fuel_canopy") out = PropType::FuelCanopy;
        else if (text == "fuel_pump") out = PropType::FuelPump;
        else if (text == "wire_fence") out = PropType::WireFence;
        else if (text == "hedge") out = PropType::Hedge;
        else if (text == "shed") out = PropType::Shed;
        else if (text == "utility_pole") out = PropType::UtilityPole;
        else if (text == "signal_head") out = PropType::SignalHead;
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
        PlaceBuildings(world, warnings);
        PlaceTrees(world, warnings);
        PlaceAvenues(world, warnings);
        PlaceSigns(world);
        PlaceProps(world, warnings);
        PlaceVehicles(world, warnings);
        PlaceStreetParking(world);
        PlaceDelineators(world);
        PlaceTrafficSignals(world);
        PlacePlots(world);
        PlaceUtilityPoles(world);
        PlaceBushes(world);
        PlaceGardenTrees(world);
        PlaceMeadowTrees(world);
        BuildGrids(world);
    }

    void ObjectPlacement::PlaceMeadowTrees(const MapWorld& world)
    {
        // Solitary trees and small clumps in the meadows: without them the open country between
        // the villages is a bare lawn.
        const MapGround& ground = world.Ground();
        const TerrainField& terrain = world.Terrain();
        const std::size_t existing = trees_.size();
        const auto nearTree = [&](const Vector2& p, const float radius) {
            for (std::size_t i = 0; i < existing; ++i) {
                const PlacedTree& t = trees_[i];
                if (std::fabs(t.position.X - p.X) > radius || std::fabs(t.position.Z - p.Y) > radius) continue;
                if (Vector2::DistanceSquared(Vector2(t.position.X, t.position.Z), p) < radius * radius) return true;
            }
            return false;
        };
        const TreeSpecies species[] = {TreeSpecies::Oak, TreeSpecies::Linden, TreeSpecies::Maple, TreeSpecies::Birch};
        constexpr float kCell = 130.0f;
        const int cols = static_cast<int>((terrain.MaxX() - terrain.MinX()) / kCell);
        const int rows = static_cast<int>((terrain.MaxZ() - terrain.MinZ()) / kCell);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (Hash01(c, r, 1301u) > 0.42f) continue;
                const float jx = Hash01(c, r, 733u), jz = Hash01(c, r, 977u);
                const Vector2 centre(terrain.MinX() + (static_cast<float>(c) + jx) * kCell,
                                     terrain.MinZ() + (static_cast<float>(r) + jz) * kCell);
                if (terrain.RegionAt(centre.X, centre.Y) != RegionType::Meadow) continue;
                const int clump = Hash01(c, r, 613u) < 0.3f ? 3 : 1;
                for (int i = 0; i < clump; ++i) {
                    const float ox = (Hash01(c * 7 + i, r, 191u) - 0.5f) * 16.0f;
                    const float oz = (Hash01(c, r * 7 + i, 197u) - 0.5f) * 16.0f;
                    const Vector2 p(centre.X + ox, centre.Y + oz);
                    if (!terrain.Contains(p.X, p.Y) || terrain.RegionAt(p.X, p.Y) != RegionType::Meadow) continue;
                    if (InsideBuilding(p, 6.0f) || !ClearOfRoads(world, p, 8.0f) || nearTree(p, 18.0f)) continue;
                    PlacedTree t;
                    t.species = species[(static_cast<unsigned>(c * 13 + r * 7 + i)) % 4u];
                    t.scale = 0.9f + 0.4f * Hash01(c + i, r, 419u);
                    t.seed = static_cast<unsigned>(c * 1009 + r * 31 + i) + 90000u;
                    t.rotationRad = Hash01(static_cast<int>(t.seed), 3, 77u) * 2.0f * kPi;
                    t.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                    trees_.push_back(t);
                }
            }
        }
    }

    void ObjectPlacement::PlaceGardenTrees(const MapWorld& world)
    {
        // Fruit and shade trees in the gardens behind village and town houses: the plots were
        // fenced but empty lawns, which is the one thing a Czech village never is.
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        const std::size_t existing = trees_.size();
        const auto nearTree = [&](const Vector2& p, const float radius) {
            for (std::size_t i = 0; i < existing; ++i) {
                const PlacedTree& t = trees_[i];
                if (std::fabs(t.position.X - p.X) > radius || std::fabs(t.position.Z - p.Y) > radius) continue;
                if (Vector2::DistanceSquared(Vector2(t.position.X, t.position.Z), p) < radius * radius) return true;
            }
            return false;
        };
        const TreeSpecies orchard[] = {TreeSpecies::Maple, TreeSpecies::Birch, TreeSpecies::Linden, TreeSpecies::Maple};
        const std::size_t buildingCount = buildings_.size();
        for (std::size_t bi = 0; bi < buildingCount; ++bi) {
            const PlacedBuilding& b = buildings_[bi];
            const std::string& type = b.spec->type;
            if (type != "house" && type != "cottage") continue;
            const Vector2 centre(b.position.X, b.position.Z);
            RoadHit hit;
            if (!network.NearestRoad(centre, 60.0f, hit)) continue;
            Vector2 tangent(hit.sample.tangent.X, hit.sample.tangent.Z);
            if (tangent.LengthSquared() < 1e-6f) continue;
            tangent.Normalize();
            const Vector2 right(-tangent.Y, tangent.X);
            const float side = hit.lateral > 0.0f ? 1.0f : -1.0f;
            const Vector2 away = right * side;                        // from the road into the garden
            const unsigned seed = b.spec->seed;
            const int count = 1 + static_cast<int>(Hash01(static_cast<int>(seed), 21, 409u) * 2.99f);   // 1..3
            for (int i = 0; i < count; ++i) {
                const float along = (Hash01(static_cast<int>(seed) + i, 31, 613u) - 0.5f) * 2.0f * (b.halfWidth + 3.0f);
                const float back = std::max(b.halfDepth, b.halfWidth) + 3.5f + Hash01(static_cast<int>(seed) + i, 41, 821u) * 7.0f;
                const Vector2 p = centre + away * back + tangent * along;
                if (!world.Terrain().Contains(p.X, p.Y) || InsideBuilding(p, 3.0f) || !ClearOfRoads(world, p, 3.0f) || nearTree(p, 6.0f)) continue;
                if (world.Terrain().RegionAt(p.X, p.Y) == RegionType::Square) continue;
                PlacedTree t;
                t.species = orchard[(seed + static_cast<unsigned>(i)) % 4u];
                t.scale = 0.7f + 0.25f * Hash01(static_cast<int>(seed) + i, 51, 937u);
                t.seed = seed * 31u + static_cast<unsigned>(i);
                t.rotationRad = Hash01(static_cast<int>(t.seed), 3, 77u) * 2.0f * kPi;
                t.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                trees_.push_back(t);
            }
        }
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

    void ObjectPlacement::PlaceBuildings(const MapWorld& world, std::vector<std::string>& warnings)
    {
        const MapGround& ground = world.Ground();
        std::vector<PlacedBuilding> planned;
        std::vector<Vector2> resolvedCentres;
        std::vector<bool> active;
        planned.reserve(world.Data().objects.buildings.size());
        resolvedCentres.reserve(world.Data().objects.buildings.size());
        active.reserve(world.Data().objects.buildings.size());
        for (const BuildingSpec& spec : world.Data().objects.buildings) {
            PlacedBuilding b;
            b.spec = &spec;
            b.headingRad = spec.rotationDeg * kPi / 180.0f;
            b.halfWidth = spec.width * 0.5f;
            b.halfDepth = spec.depth * 0.5f;
            b.height = spec.eavesHeight;
            const float pitch = spec.roofPitchDeg * kPi / 180.0f;
            b.roofHeight = std::tan(pitch) * std::min(b.halfDepth, b.halfWidth);
            b.position = Vector3(spec.position.X, 0.0f, spec.position.Y);
            planned.push_back(b);
            resolvedCentres.push_back(spec.position);
            active.push_back(true);
        }
        for (std::size_t i = 0; i < planned.size(); ++i) {
            PlacedBuilding b = planned[i];
            const BuildingSpec& spec = *b.spec;
            Vector2 centre = spec.position;
            // A gatehouse is built over its road on purpose: the road runs through the arch.
            if (b.PassageHalfWidth() == 0.0f && !BuildingRoadClear(world, b, centre)) {
                // Some authored rows cross a different road from the one they face. Keep the
                // building in its neighbourhood by finding the closest free plot nearby.
                RoadHit hit;
                Vector2 away(1.0f, 0.0f);
                if (world.Roads().NearestRoad(centre, 100.0f, hit)) {
                    away = centre - Vector2(hit.sample.position.X, hit.sample.position.Z);
                    if (away.LengthSquared() < 0.01f) {
                        away = Vector2(-hit.sample.tangent.Z, hit.sample.tangent.X);
                    }
                }
                const float baseAngle = std::atan2(away.Y, away.X);
                bool moved = false;
                for (float radius = 2.0f; radius <= 80.0f && !moved; radius += 2.0f) {
                    for (int direction = 0; direction < 16; ++direction) {
                        const float angle = baseAngle + static_cast<float>(direction) * (2.0f * kPi / 16.0f);
                        const Vector2 candidate = spec.position + Vector2(std::cos(angle), std::sin(angle)) * radius;
                        if (!BuildingRoadClear(world, b, candidate)) continue;
                        bool occupied = false;
                        for (std::size_t other = 0; other < planned.size(); ++other) {
                            if (other == i || !active[other]) continue;
                            if (BuildingFootprintsOverlap(b, candidate, planned[other], resolvedCentres[other])) {
                                occupied = true;
                                break;
                            }
                        }
                        if (occupied) continue;
                        centre = candidate;
                        moved = true;
                        break;
                    }
                }
                if (!moved) {
                    active[i] = false;
                    warnings.push_back("objects.buildings: no road-clear position for " + spec.type + " near (" +
                                       std::to_string(spec.position.X) + ", " + std::to_string(spec.position.Y) + ")");
                    continue;
                }
            }
            resolvedCentres[i] = centre;
            // Foundation: highest corner defines the floor, walls extend down to the lowest corner.
            const Vector2 fwd = DirectionFromHeading(b.headingRad);
            const Vector2 right(-fwd.Y, fwd.X);
            float lowest = std::numeric_limits<float>::max();
            float highest = -lowest;
            for (const float sx : {-1.0f, 1.0f}) {
                for (const float sz : {-1.0f, 1.0f}) {
                    const Vector2 corner = centre + right * (sx * b.halfWidth) + fwd * (sz * b.halfDepth);
                    const float h = ground.HeightAt(corner.X, corner.Y);
                    lowest = std::min(lowest, h);
                    highest = std::max(highest, h);
                }
            }
            const float centreHeight = ground.HeightAt(centre.X, centre.Y);
            const float floor = std::max(centreHeight, highest - 0.35f);
            b.position = Vector3(centre.X, floor + 0.05f, centre.Y);
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
            t.scale = spec.scale;
            t.seed = spec.seed;
            t.rotationRad = Hash01(static_cast<int>(spec.seed), 3, 11u) * 2.0f * kPi;
            Vector2 position = spec.position;
            const auto clear = [&](const Vector2& p) {
                return world.Terrain().Contains(p.X, p.Y) &&
                       ClearOfRoads(world, p, t.TrunkRadius() + 0.75f) &&
                       !InsideBuilding(p, t.TrunkRadius() + 0.75f);
            };
            if (!clear(position)) {
                bool moved = false;
                for (float radius = 2.0f; radius <= 30.0f && !moved; radius += 2.0f) {
                    for (int direction = 0; direction < 16; ++direction) {
                        const float angle = static_cast<float>(direction) * (2.0f * kPi / 16.0f);
                        const Vector2 candidate = spec.position + Vector2(std::cos(angle), std::sin(angle)) * radius;
                        if (!clear(candidate)) continue;
                        position = candidate;
                        moved = true;
                        break;
                    }
                }
                if (!moved) {
                    warnings.push_back("objects.trees: no road-clear position near (" +
                                       std::to_string(spec.position.X) + ", " + std::to_string(spec.position.Y) + ")");
                    continue;
                }
            }
            t.position = Vector3(position.X, ground.HeightAt(position.X, position.Y), position.Y);
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
                    const float edgeDistance = DistanceToPolygonEdge(p, forest.polygon);
                    if (edgeDistance < forest.margin * 0.5f) continue;
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
                    // A forest does not end in a wall of full-grown timber: the trees at the edge
                    // are younger and shorter, and the canopy climbs over the first twenty metres.
                    // Without the taper the boundary reads as a cut-out against the sky from every
                    // road that runs beside it.
                    const float edge = std::clamp(edgeDistance / 20.0f, 0.0f, 1.0f);
                    const float taper = 0.55f + 0.45f * (edge * edge * (3.0f - 2.0f * edge));
                    t.scale = (0.75f + 0.5f * Hash01(col, row, forest.seed + 53u)) * taper;
                    t.rotationRad = Hash01(col, row, forest.seed + 67u) * 2.0f * kPi;
                    t.seed = static_cast<unsigned>(col * 7919 + row * 104729) + forest.seed;
                    t.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                    trees_.push_back(t);
                    // A small, irregular understory near a few trunks breaks up the bare
                    // forest floor. Keep it inside the same polygon and outside the road
                    // clearance, so it cannot spill onto the carriageway or a building.
                    if (edgeDistance > 6.0f && Hash01(col, row, forest.seed + 83u) < 0.05f) {
                        const float angle = Hash01(col, row, forest.seed + 89u) * 2.0f * kPi;
                        const float offset = 4.0f + 3.0f * Hash01(col, row, forest.seed + 97u);
                        const Vector2 under(p.X + std::cos(angle) * offset, p.Y + std::sin(angle) * offset);
                        if (PointInPolygon(under, forest.polygon) && ClearOfRoads(world, under, forest.margin) &&
                            !InsideBuilding(under, 2.0f) && world.Terrain().Contains(under.X, under.Y)) {
                            PlacedTree bush;
                            bush.species = TreeSpecies::Bush;
                            bush.position = Vector3(under.X, ground.HeightAt(under.X, under.Y), under.Y);
                            bush.scale = 0.36f + 0.26f * Hash01(col, row, forest.seed + 101u);
                            bush.rotationRad = angle;
                            bush.seed = t.seed ^ 0x6ac690c5u;
                            bush.collidable = false;
                            trees_.push_back(bush);
                            // Some understory grows in little groups instead of isolated
                            // dots. Keep the companion lower than the first shrub and use
                            // the same clearance rules, so snow and roadside behaviour
                            // continue to follow the existing bush path.
                            if (Hash01(col, row, forest.seed + 107u) < 0.58f) {
                                const float companionAngle = angle + 1.4f + 1.4f * Hash01(col, row, forest.seed + 109u);
                                const float companionDistance = 1.4f + 1.2f * Hash01(col, row, forest.seed + 113u);
                                const Vector2 companion(under.X + std::cos(companionAngle) * companionDistance,
                                                        under.Y + std::sin(companionAngle) * companionDistance);
                                if (PointInPolygon(companion, forest.polygon) && ClearOfRoads(world, companion, forest.margin) &&
                                    !InsideBuilding(companion, 2.0f) && world.Terrain().Contains(companion.X, companion.Y)) {
                                    PlacedTree low = bush;
                                    low.position = Vector3(companion.X, ground.HeightAt(companion.X, companion.Y), companion.Y);
                                    low.scale = 0.25f + 0.18f * Hash01(col, row, forest.seed + 127u);
                                    low.rotationRad = companionAngle;
                                    low.seed = t.seed ^ 0x18baf2d3u;
                                    trees_.push_back(low);
                                }
                            }
                        }
                    }
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
                    if (!ClearOfRoads(world, p, t.TrunkRadius() + 0.75f)) continue;
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

    void ObjectPlacement::PlaceStreetParking(const MapWorld& world)
    {
        // Cars parked along the kerb of town streets, clear of the carriageway so the traffic
        // never meets them: the inner flank sits just outside the paved edge, on the sidewalk
        // strip, which is how cars stand in Czech towns.
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        const Sim::CarStyle::Body bodies[] = {Sim::CarStyle::Body::Hatchback, Sim::CarStyle::Body::Estate, Sim::CarStyle::Body::Hatchback,
                                              Sim::CarStyle::Body::Sedan, Sim::CarStyle::Body::Suv, Sim::CarStyle::Body::Van};
        for (const Road& road : network.Roads()) {
            const RoadClass cls = road.spec->roadClass;
            if (cls != RoadClass::Local && cls != RoadClass::Residential) continue;
            int slot = 0;
            for (float s = 18.0f; s < road.curve.Length() - 12.0f; s += 18.0f, ++slot) {
                const RoadSample sample = road.curve.Evaluate(s);
                if (!sample.urban) continue;
                const unsigned key = static_cast<unsigned>(road.index) * 131u + static_cast<unsigned>(slot);
                if (Hash01(static_cast<int>(key), 11, 613u) > 0.55f) continue;
                bool nearJunction = false;
                for (const Intersection& inter : network.Intersections()) {
                    for (const Approach& a : inter.approaches) {
                        if (a.road == road.index && std::fabs(a.nodeS - s) < a.setback + 12.0f) nearJunction = true;
                    }
                }
                if (nearJunction) continue;
                Vector2 tangent(sample.tangent.X, sample.tangent.Z);
                if (tangent.LengthSquared() < 1e-6f) continue;
                tangent.Normalize();
                const Vector2 right(-tangent.Y, tangent.X);
                const float side = Hash01(static_cast<int>(key), 5, 811u) < 0.5f ? -1.0f : 1.0f;
                const Sim::CarStyle::Body body = bodies[key % 6u];
                const Sim::CarStyle style = Sim::CarStyle::Preset(body, key);
                const float lateral = road.profile.HalfPavedWidth() + 0.35f + 0.5f * style.width;
                const Vector2 centre(sample.position.X, sample.position.Z);
                const Vector2 p = centre + right * (side * lateral);
                if (!world.Terrain().Contains(p.X, p.Y) || InsideBuilding(p, 0.6f)) continue;
                // The kerb strip is part of the road corridor, so `onRoad` is true there; what
                // matters is that the whole car stands outside the paved carriageway.
                if (ground.Sample(p.X, p.Y).distanceToPavedEdge < 0.3f) continue;
                if (world.Roads().IntersectionContaining(p) >= 0) continue;
                PlacedVehicle v;
                v.body = body;
                v.seed = 9000u + key;
                // Parked with the traffic: the nose points along +s on the right, against it on the left.
                const Vector2 nose = side > 0.0f ? tangent : Vector2(-tangent.X, -tangent.Y);
                v.headingRad = HeadingFromDirection(nose.X, nose.Y);
                v.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                vehicles_.push_back(v);
            }
        }
    }

    void ObjectPlacement::PlaceTrafficSignals(const MapWorld& world)
    {
        const MapGround& ground = world.Ground();
        const RoadNetwork& network = world.Roads();
        for (std::size_t i = 0; i < network.Intersections().size(); ++i) {
            const Intersection& inter = network.Intersections()[i];
            if (!inter.signals.enabled) {
                continue;
            }
            for (const Approach& a : inter.approaches) {
                if (a.signalGroup < 0 || a.road < 0) continue;
                const Road& road = network.Roads()[static_cast<std::size_t>(a.road)];
                // Stop line, then out to the kerb on the approaching driver's right. The driver
                // travels towards the node, that is along -direction, so their right is
                // (direction.z, -direction.x).
                const Vector2 centre(inter.center.X, inter.center.Z);
                const Vector2 stopLine = centre + a.direction * (a.setback + 0.8f);
                const Vector2 right(a.direction.Y, -a.direction.X);
                const float lateral = road.profile.HalfPavedWidth() + 0.9f;
                const Vector2 p = stopLine + right * lateral;

                PlacedSignal signal;
                signal.position = Vector3(p.X, ground.HeightAt(p.X, p.Y), p.Y);
                // The lenses face the traffic coming towards the node, i.e. along +direction.
                signal.headingRad = HeadingFromDirection(a.direction.X, a.direction.Y);
                signal.intersection = static_cast<int>(i);
                signal.group = a.signalGroup;
                signals_.push_back(signal);

                PlacedProp mast;
                mast.type = PropType::SignalHead;
                mast.position = signal.position;
                mast.headingRad = signal.headingRad;
                props_.push_back(mast);
            }
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
