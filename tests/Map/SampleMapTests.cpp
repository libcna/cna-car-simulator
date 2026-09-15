#include "CarSim/Collision/CollisionWorld.hpp"
#include "CarSim/Sim/Vehicle.hpp"
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace CarSim;

namespace
{
    std::string LipovaDirectory() { return Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"); }

    /// Shortest distance from a point to a polygon's boundary (the placement code has its own
    /// copy of this; duplicating four lines here is cheaper than widening the map API for a test).
    float DistanceToEdge(const Microsoft::Xna::Framework::Vector2& p,
                         const std::vector<Microsoft::Xna::Framework::Vector2>& polygon)
    {
        float best = 1e30f;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            const auto a = polygon[j];
            const auto b = polygon[i];
            const auto ab = b - a;
            const float lengthSq = ab.X * ab.X + ab.Y * ab.Y;
            const float t = lengthSq > 1e-9f
                                ? std::clamp(((p.X - a.X) * ab.X + (p.Y - a.Y) * ab.Y) / lengthSq, 0.0f, 1.0f)
                                : 0.0f;
            const auto closest = a + ab * t;
            const float dx = p.X - closest.X;
            const float dy = p.Y - closest.Y;
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        return best;
    }
}

TEST(SampleMap, LipovaLoadsAndIsFullyConnected)
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors, &warnings);
    for (const auto& e : errors) {
        ADD_FAILURE() << e;
    }
    ASSERT_TRUE(world);
    EXPECT_EQ(world->Data().info.id, "lipova");
    const auto& roads = world->Roads();
    const auto& lanes = world->Lanes();
    EXPECT_GT(roads.TotalLength(), 14000.0f);
    EXPECT_LT(roads.TotalLength(), 40000.0f);   // one region, not a continent
    EXPECT_GE(roads.Intersections().size(), 10u);
    for (const auto& inter : roads.Intersections()) {
        for (const auto& a : inter.approaches) {
            EXPECT_GE(a.piece, 0) << "approach without piece at node " << world->Data().nodes[static_cast<std::size_t>(inter.node)].id;
        }
    }
    ASSERT_FALSE(lanes.Lanes().empty());
    EXPECT_EQ(lanes.Reachable(0).size(), lanes.Lanes().size());
    for (const auto& lane : lanes.Lanes()) {
        EXPECT_FALSE(lane.outgoingLinks.empty()) << "lane " << lane.id << " dead-ends";
    }
    // Every spawn sits on a lane.
    ASSERT_FALSE(world->Data().traffic.playerSpawns.empty());
    for (const auto& spawn : world->Data().traffic.playerSpawns) {
        const int lane = lanes.NearestLane(spawn.position, spawn.headingDeg * 3.14159265f / 180.0f, 4.0f);
        EXPECT_GE(lane, 0) << "spawn " << spawn.name;
    }
    // Build time budget for the sample map (plan section 17).
    EXPECT_LT(world->Stats().loadSeconds, 6.0);
}

TEST(SampleMap, TownIsUrbanAndCountrysideIsNot)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    const auto* main = world->Data().FindRoad("main");
    ASSERT_NE(main, nullptr);
    const auto& roads = world->Roads();
    const Map::Road* road = nullptr;
    for (const auto& r : roads.Roads()) {
        if (r.spec == main) road = &r;
    }
    ASSERT_NE(road, nullptr);
    // The square is inside the built-up area; the open road east of Březí is not.
    Map::RoadHit hit;
    ASSERT_TRUE(roads.NearestRoad(Microsoft::Xna::Framework::Vector2(-100.0f, 3.0f), 10.0f, hit));
    EXPECT_FLOAT_EQ(road->SpeedLimitAt(hit.s), 50.0f);
    ASSERT_TRUE(roads.NearestRoad(Microsoft::Xna::Framework::Vector2(1680.0f, -760.0f), 60.0f, hit));
    EXPECT_FLOAT_EQ(road->SpeedLimitAt(hit.s), 90.0f);
}

TEST(SampleMap, RightOfWayIsAntisymmetric)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    const auto& lanes = world->Lanes();
    int yields = 0;
    for (const auto& link : lanes.Links()) {
        for (const int other : link.yieldTo) {
            ++yields;
            const auto& m = lanes.LinkAt(other);
            EXPECT_TRUE(std::find(m.yieldTo.begin(), m.yieldTo.end(), link.id) == m.yieldTo.end())
                << "links " << link.id << " and " << other << " yield to each other";
            EXPECT_TRUE(std::find(link.conflicts.begin(), link.conflicts.end(), other) != link.conflicts.end());
        }
    }
    EXPECT_GT(yields, 20);
}

TEST(SampleMap, PlotsAndUtilityPolesAreGeneratedClearOfBuildingsAndRoads)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    int fences = 0, hedges = 0, wire = 0, sheds = 0, poles = 0;
    for (const auto& p : world->Objects().Props()) {
        switch (p.type) {
            case Map::PropType::Fence: ++fences; break;
            case Map::PropType::Hedge: ++hedges; break;
            case Map::PropType::WireFence: ++wire; break;
            case Map::PropType::Shed: ++sheds; break;
            case Map::PropType::UtilityPole: ++poles; break;
            default: break;
        }
        if (p.type == Map::PropType::Shed || p.type == Map::PropType::UtilityPole) {
            const Microsoft::Xna::Framework::Vector2 at(p.position.X, p.position.Z);
            EXPECT_FALSE(world->Objects().InsideBuilding(at, 0.5f));
            float height = 0.0f;
            Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
            float edge = 0.0f;
            const bool onRoad = world->Roads().RoadSurfaceAt(at, height, surface, edge) && edge < 0.0f;
            EXPECT_FALSE(onRoad) << "prop on the paved road at " << at.X << ", " << at.Y;
        }
    }
    EXPECT_GT(fences + hedges + wire, 60) << "houses should have street-side fences";
    EXPECT_GT(hedges, 5);
    EXPECT_GT(wire, 5);
    EXPECT_GT(sheds, 10);
    EXPECT_GT(poles, 40);
}

TEST(SampleMap, BushesLineRuralVergesAndForestEdges)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    int bushes = 0;
    for (const auto& t : world->Objects().Trees()) {
        if (t.species != Map::TreeSpecies::Bush) continue;
        ++bushes;
        EXPECT_LT(t.Height(), 4.0f);
        const Microsoft::Xna::Framework::Vector2 at(t.position.X, t.position.Z);
        EXPECT_FALSE(world->Objects().InsideBuilding(at, 1.0f));
        float height = 0.0f;
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
        float edge = 0.0f;
        const bool onPaved = world->Roads().RoadSurfaceAt(at, height, surface, edge) && edge < 0.0f;
        EXPECT_FALSE(onPaved);
    }
    EXPECT_GT(bushes, 300);
}

TEST(SampleMap, TheSquareIsPavedAndLinedWithTownHouses)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    // The naměstí is a paved region: the ground there drives and sounds like cobbles.
    const Map::RegionSpec* square = nullptr;
    for (const auto& region : world->Data().terrain.regions) {
        if (region.type == Map::RegionType::Square) square = &region;
    }
    ASSERT_NE(square, nullptr) << "the sample map has a paved square";
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& p : square->polygon) {
        minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
        minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
    }
    EXPECT_GT((maxX - minX) * (maxZ - minZ), 2000.0f) << "a square worth the name";
    const float centreX = 0.5f * (minX + maxX);
    const float centreZ = 0.5f * (minZ + maxZ);
    EXPECT_EQ(world->Terrain().RegionAt(centreX, centreZ), Map::RegionType::Square);
    EXPECT_EQ(world->Ground().Sample(centreX, centreZ).surface, Sim::SurfaceType::Cobbles);
    EXPECT_EQ(world->Ground().Sample(minX - 40.0f, centreZ).surface, Sim::SurfaceType::Grass) << "outside the square it is grass again";

    // Buildings line at least three sides, and one of them is the church.
    int west = 0, east = 0, north = 0, church = 0;
    for (const auto& b : world->Objects().Buildings()) {
        const float x = b.position.X;
        const float z = b.position.Z;
        if (z < minZ - 14.0f || z > maxZ + 14.0f) continue;
        if (x > minX - 12.0f && x < minX + 2.0f) ++west;
        if (x > maxX - 2.0f && x < maxX + 12.0f) ++east;
        if (z < minZ + 2.0f && x > minX - 6.0f && x < maxX + 6.0f) ++north;
        if (b.spec && b.spec->type == "church" && x > minX && x < maxX && z > minZ && z < maxZ) ++church;
    }
    // A stone memorial stands on the square and is solid.
    int memorials = 0;
    for (const auto& prop : world->Objects().Props()) {
        if (prop.type != Map::PropType::Memorial) continue;
        ++memorials;
        EXPECT_EQ(world->Terrain().RegionAt(prop.position.X, prop.position.Z), Map::RegionType::Square);
    }
    EXPECT_EQ(memorials, 1);

    EXPECT_GE(west, 3) << "town houses along the west side";
    EXPECT_GE(east, 3) << "town houses along the east side";
    EXPECT_GE(north, 2) << "town houses along the north side";
    EXPECT_EQ(church, 1) << "the church stands on the square";
}

TEST(SampleMap, ParkedCarsStandOnTheSquareClearOfBuildingsAndRoads)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    const auto& parked = world->Objects().Vehicles();
    const std::size_t authored = world->Data().objects.vehicles.size();
    ASSERT_GE(authored, 10u) << "the square has a few cars parked on it";
    ASSERT_GE(parked.size(), authored) << "authored cars come first, generated street parking after";
    int onSquare = 0;
    for (std::size_t i = 0; i < authored; ++i) {
        const auto& car = parked[i];
        const float x = car.position.X;
        const float z = car.position.Z;
        const Map::RegionType region = world->Terrain().RegionAt(x, z);
        EXPECT_TRUE(region == Map::RegionType::Square || region == Map::RegionType::Yard) << "authored cars stand on paving";
        if (region == Map::RegionType::Square) ++onSquare;
        const auto sample = world->Ground().Sample(x, z);
        EXPECT_FALSE(sample.onRoad) << "a parked car must not stand in the carriageway";
        EXPECT_NEAR(car.position.Y, sample.height, 0.01f) << "parked cars sit on the ground";
        for (const auto& b : world->Objects().Buildings()) {
            // Local frame of the footprint: +z along the facade heading, +x to its right.
            const float dx = x - b.position.X;
            const float dz = z - b.position.Z;
            const float fx = std::sin(b.headingRad);
            const float fz = -std::cos(b.headingRad);
            const float along = dx * fx + dz * fz;              // depth axis
            const float across = dx * -fz + dz * fx;            // width axis
            const bool insideFootprint = std::fabs(along) < b.halfDepth + 1.2f && std::fabs(across) < b.halfWidth + 1.2f;
            EXPECT_FALSE(insideFootprint) << "parked car inside a building footprint";
        }
    }
    EXPECT_GE(onSquare, 10) << "most of the authored cars are parked on the square";

    // Cars generated along the town streets stand clear of the carriageway and of buildings.
    for (std::size_t i = authored; i < parked.size(); ++i) {
        const auto& car = parked[i];
        const auto sample = world->Ground().Sample(car.position.X, car.position.Z);
        EXPECT_GE(sample.distanceToPavedEdge, 0.3f) << "a parked car reaches into the carriageway";
        EXPECT_LT(world->Roads().IntersectionContaining(Microsoft::Xna::Framework::Vector2(car.position.X, car.position.Z)), 0) << "parked inside a junction";
    }

    // Every parked car is solid.
    Collision::CollisionWorld collision;
    collision.Build(*world);
    std::size_t vehicles = 0;
    for (const auto& c : collision.Statics()) {
        if (c.kind == Collision::ColliderKind::Vehicle) ++vehicles;
    }
    EXPECT_EQ(vehicles, parked.size());
}

TEST(SampleMap, DrivingOntoTheSquareRollsOnCobbles)
{
    // The paved square is not only a texture: the wheels report cobbles there, which is what
    // the rolling-noise layer and the tyre model read.
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    const Map::RegionSpec* square = nullptr;
    for (const auto& region : world->Data().terrain.regions) {
        if (region.type == Map::RegionType::Square) square = &region;
    }
    ASSERT_NE(square, nullptr);
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& p : square->polygon) {
        minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
        minZ = std::min(minZ, p.Y); maxZ = std::max(maxZ, p.Y);
    }
    // A clear patch of paving, away from the church and the parked rows.
    const float x = 0.5f * (minX + maxX) + 8.0f;
    const float z = maxZ - 14.0f;
    Sim::VehicleDefinition def = Sim::MakeReferenceVehicle();
    Sim::Vehicle vehicle(def, Sim::TransmissionMode::Automatic);
    vehicle.PlaceAt(Microsoft::Xna::Framework::Vector3(x, world->Ground().HeightAt(x, z), z), 0.0f);
    Sim::DriverControls idle;
    for (int i = 0; i < 90; ++i) {
        vehicle.Update(idle, 1.0f / 60.0f, world->Ground());
    }
    const Sim::VehicleState state = vehicle.Snapshot();
    int grounded = 0;
    for (const auto& wheel : state.wheels) {
        if (!wheel.grounded) continue;
        ++grounded;
        EXPECT_EQ(wheel.surface, Sim::SurfaceType::Cobbles);
    }
    EXPECT_EQ(grounded, 4);
}

TEST(SampleMap, GardenTreesStandBehindHousesClearOfBuildingsAndRoads)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    const auto& objects = world->Objects();
    int gardenTrees = 0;
    for (const auto& tree : objects.Trees()) {
        if (tree.species == Map::TreeSpecies::Bush) continue;
        const Microsoft::Xna::Framework::Vector2 at(tree.position.X, tree.position.Z);
        // Only look at trees standing close to a house: those are the garden ones.
        bool nearHouse = false;
        for (const auto& b : objects.Buildings()) {
            if (b.spec->type != "house" && b.spec->type != "cottage") continue;
            if (Microsoft::Xna::Framework::Vector2::DistanceSquared(at, Microsoft::Xna::Framework::Vector2(b.position.X, b.position.Z)) < 400.0f) {
                nearHouse = true;
                break;
            }
        }
        if (!nearHouse) continue;
        ++gardenTrees;
        EXPECT_FALSE(objects.InsideBuilding(at, 1.0f)) << "a tree grows through a house";
        Map::RoadHit hit;
        if (world->Roads().NearestRoad(at, 30.0f, hit)) {
            const auto& road = world->Roads().Roads()[static_cast<std::size_t>(hit.road)];
            EXPECT_GT(std::fabs(hit.lateral), road.profile.HalfTotalWidth() + 1.0f) << "a tree stands in the road";
        }
    }
    EXPECT_GT(gardenTrees, 200) << "the plots are planted";
}

TEST(SampleMap, TheFillingStationHasAPavedForecourtWithSolidCanopyColumns)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    const Map::PlacedProp* canopy = nullptr;
    int pumps = 0;
    for (const auto& prop : world->Objects().Props()) {
        if (prop.type == Map::PropType::FuelCanopy) canopy = &prop;
        if (prop.type == Map::PropType::FuelPump) ++pumps;
    }
    ASSERT_NE(canopy, nullptr) << "the map has a filling station";
    EXPECT_EQ(pumps, 2);
    // The forecourt under the canopy is a paved yard, and the pumps stand on it.
    EXPECT_EQ(world->Terrain().RegionAt(canopy->position.X, canopy->position.Z), Map::RegionType::Yard);
    EXPECT_EQ(world->Ground().Sample(canopy->position.X, canopy->position.Z).surface, Sim::SurfaceType::Concrete);
    for (const auto& prop : world->Objects().Props()) {
        if (prop.type != Map::PropType::FuelPump) continue;
        EXPECT_EQ(world->Terrain().RegionAt(prop.position.X, prop.position.Z), Map::RegionType::Yard);
    }
    // Four columns are solid; the deck itself is not.
    Collision::CollisionWorld collision;
    collision.Build(*world);
    int posts = 0;
    for (const auto& c : collision.Statics()) {
        if (c.kind != Collision::ColliderKind::Post) continue;
        if (Microsoft::Xna::Framework::Vector3::DistanceSquared(c.centre, canopy->position) < 100.0f) ++posts;
    }
    EXPECT_EQ(posts, 4);
}

TEST(SampleMap, MeadowTreesStandInTheMeadowsClearOfTheRoadsAndBuildings)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(Map::MapDirectory(CARSIM_TEST_CONTENT_DIR, "lipova"), errors);
    ASSERT_TRUE(world);
    int meadowTrees = 0;
    for (const auto& tree : world->Objects().Trees()) {
        if (tree.species == Map::TreeSpecies::Bush) continue;
        const float x = tree.position.X;
        const float z = tree.position.Z;
        if (world->Terrain().RegionAt(x, z) != Map::RegionType::Meadow) continue;
        ++meadowTrees;
        Map::RoadHit hit;
        if (world->Roads().NearestRoad(Microsoft::Xna::Framework::Vector2(x, z), 40.0f, hit)) {
            const auto& road = world->Roads().Roads()[static_cast<std::size_t>(hit.road)];
            // Avenue trees stand deliberately close; nothing may grow on the road itself.
            EXPECT_GT(std::fabs(hit.lateral), road.profile.HalfTotalWidth() + 1.5f) << "a tree stands in the road";
        }
        EXPECT_FALSE(world->Objects().InsideBuilding(Microsoft::Xna::Framework::Vector2(x, z), 4.0f));
    }
    EXPECT_GT(meadowTrees, 100) << "the meadows are planted";
}


// The ground a wheel stands on must be the surface the road was designed to have -- everywhere a
// car can drive, junctions included. Before this was measured, the carriageway kept its crossfall
// and its centreline height right up to a junction while the junction apron was a flat sloped
// plane, so the two met in a lip: 47 cm on a connector at Podhájí, where a turning car dropped a
// wheel off the pavement into the sunk terrain.
TEST(SampleMap, TheGroundFollowsTheRoadSurfaceEverywhereACarCanDrive)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world) << (errors.empty() ? "" : errors.front());
    const auto& lanes = world->Lanes();

    float worstDeviation = 0.0f;
    float worstStep = 0.0f;
    Microsoft::Xna::Framework::Vector3 worstDeviationAt{};
    Microsoft::Xna::Framework::Vector3 worstStepAt{};

    const auto walk = [&](const auto& path, const float length) {
        if (length < 2.0f) return;
        float previous = 0.0f;
        bool havePrevious = false;
        for (float s = 0.0f; s <= length; s += 1.0f) {
            const auto point = path.Evaluate(s);
            const float deviation = world->Ground().HeightAt(point.position.X, point.position.Z) - point.position.Y;
            if (std::fabs(deviation) > std::fabs(worstDeviation)) {
                worstDeviation = deviation;
                worstDeviationAt = point.position;
            }
            if (havePrevious && std::fabs(deviation - previous) > worstStep) {
                worstStep = std::fabs(deviation - previous);
                worstStepAt = point.position;
            }
            previous = deviation;
            havePrevious = true;
        }
    };
    for (const auto& lane : lanes.Lanes()) walk(lane, lane.length);
    for (const auto& link : lanes.Links()) walk(link, link.length);

    // 8 cm is a kerb's worth; the shipped map measures 3.6 cm. 6 cm of change over one metre is
    // already a thump; the shipped map measures 2.2 cm.
    EXPECT_LT(std::fabs(worstDeviation), 0.08f)
        << "ground is " << worstDeviation << " m from the road surface at ("
        << worstDeviationAt.X << ", " << worstDeviationAt.Z << ")";
    EXPECT_LT(worstStep, 0.06f)
        << "the ground steps " << worstStep << " m over one metre at ("
        << worstStepAt.X << ", " << worstStepAt.Z << ")";
}

// A junction's paved area rounds its corners outward. A fillet that cuts a notch back towards the
// node leaves the connectors that turn there partly off the pavement.
TEST(SampleMap, JunctionFilletsNeverCutBackTowardsTheNode)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world);
    for (const auto& inter : world->Roads().Intersections()) {
        const Microsoft::Xna::Framework::Vector2 centre(inter.center.X, inter.center.Z);
        const auto& patch = inter.patch;
        ASSERT_GE(patch.size(), 3u);
        // Every patch vertex must be at least as far from the node as the chord between the two
        // approach-edge ends around it. Checking that the polygon is star-shaped about the node
        // is the same statement and much simpler: walking the boundary, no vertex may lie inside
        // the triangle formed by the node and its two neighbours.
        for (std::size_t i = 0; i < patch.size(); ++i) {
            const auto& previous = patch[(i + patch.size() - 1) % patch.size()];
            const auto& here = patch[i];
            const auto& next = patch[(i + 1) % patch.size()];
            const auto chord = next - previous;
            const float length = chord.Length();
            if (length < 1e-4f) continue;
            Microsoft::Xna::Framework::Vector2 outward(-chord.Y / length, chord.X / length);
            if (Microsoft::Xna::Framework::Vector2::Dot(outward, previous - centre) < 0.0f) {
                outward = outward * -1.0f;
            }
            const float depth = Microsoft::Xna::Framework::Vector2::Dot(here - previous, outward);
            EXPECT_GT(depth, -0.35f)
                << "the patch at node " << world->Data().nodes[static_cast<std::size_t>(inter.node)].id
                << " cuts " << -depth << " m back towards the node at (" << here.X << ", " << here.Y << ")";
        }
    }
}

// A forest that ends in a wall of full-grown timber reads as a cut-out against the sky from every
// road beside it. The trees at the edge should be shorter than the ones in the middle.
TEST(SampleMap, ForestEdgesTaperInsteadOfEndingInAWall)
{
    std::vector<std::string> errors;
    auto world = Map::MapWorld::Load(LipovaDirectory(), errors);
    ASSERT_TRUE(world) << (errors.empty() ? "" : errors.front());
    const auto& forests = world->Data().objects.forests;
    ASSERT_FALSE(forests.empty());

    // The biggest forest: compare the trees within 8 m of its boundary with those well inside it.
    const auto& forest = *std::max_element(forests.begin(), forests.end(),
                                           [](const auto& a, const auto& b) { return a.polygon.size() < b.polygon.size(); });
    double edgeScaleSum = 0.0;
    double coreScaleSum = 0.0;
    int edgeCount = 0;
    int coreCount = 0;
    for (const auto& tree : world->Objects().Trees()) {
        const Microsoft::Xna::Framework::Vector2 p(tree.position.X, tree.position.Z);
        if (!Map::PointInPolygon(p, forest.polygon)) continue;
        const float distance = DistanceToEdge(p, forest.polygon);
        if (distance < 8.0f) {
            edgeScaleSum += tree.scale;
            ++edgeCount;
        } else if (distance > 60.0f) {
            coreScaleSum += tree.scale;
            ++coreCount;
        }
    }
    ASSERT_GT(edgeCount, 50) << "not enough trees near the forest edge to say anything";
    ASSERT_GT(coreCount, 500) << "not enough trees inside the forest to compare against";
    const double edgeMean = edgeScaleSum / edgeCount;
    const double coreMean = coreScaleSum / coreCount;
    std::cout << "  forest edge trees " << edgeMean << " vs core " << coreMean << " (" << edgeCount
              << " / " << coreCount << " trees)\n";
    EXPECT_LT(edgeMean, coreMean * 0.85) << "the forest edge is as tall as its middle";
    EXPECT_GT(edgeMean, coreMean * 0.4) << "the forest edge has become scrub";
}
