// map-validate: loads a map directory, builds the runtime structures and reports problems.
//
//   carsim-mapvalidate <map-directory> [--quiet]
//   carsim-mapvalidate --content <content-root> --map <name> [--quiet]
//
// Exit code 0 = valid (warnings allowed), 1 = errors found or the map failed to build.
#include "CarSim/Map/MapDocument.hpp"
#include "CarSim/Map/MapWorld.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace CarSim;

namespace
{
    int Usage()
    {
        std::cerr << "usage: carsim-mapvalidate <map-directory> [--quiet]\n"
                     "       carsim-mapvalidate --content <content-root> --map <name> [--quiet]\n";
        return 2;
    }
}

int main(int argc, char** argv)
{
    std::string directory;
    std::string content;
    std::string map;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--content" && i + 1 < argc) {
            content = argv[++i];
        } else if (arg == "--map" && i + 1 < argc) {
            map = argv[++i];
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (!arg.empty() && arg[0] != '-') {
            directory = arg;
        } else {
            return Usage();
        }
    }
    if (directory.empty()) {
        if (content.empty() || map.empty()) {
            return Usage();
        }
        directory = Map::MapDirectory(content, map);
    }

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    auto world = Map::MapWorld::Load(directory, errors, &warnings);
    for (const auto& w : warnings) {
        std::cout << "warning: " << w << "\n";
    }
    for (const auto& e : errors) {
        std::cout << "error: " << e << "\n";
    }
    if (!world) {
        std::cout << "map-validate: FAILED (" << errors.size() << " error(s))\n";
        return 1;
    }

    const auto& roads = world->Roads();
    const auto& lanes = world->Lanes();
    int problems = 0;

    // Structural checks on the built network.
    for (const auto& inter : roads.Intersections()) {
        const auto& node = world->Data().nodes[static_cast<std::size_t>(inter.node)];
        for (const auto& a : inter.approaches) {
            if (a.piece < 0) {
                std::cout << "error: intersection at node '" << node.id << "': approach of road '"
                          << roads.Roads()[static_cast<std::size_t>(a.road)].spec->id
                          << "' has no road piece (road too short for its setback " << a.setback << " m)\n";
                ++problems;
            }
        }
        if (inter.patch.size() < 3 || Map::PolygonArea(inter.patch) < 1.0f) {
            std::cout << "error: intersection at node '" << node.id << "': degenerate patch\n";
            ++problems;
        }
    }
    float maxGrade = 0.0f;
    std::string maxGradeRoad;
    for (const auto& road : roads.Roads()) {
        const auto& samples = road.curve.Samples();
        for (std::size_t i = 1; i < samples.size(); ++i) {
            const float run = samples[i].s - samples[i - 1].s;
            if (run < 0.1f) continue;
            const float grade = std::fabs(samples[i].position.Y - samples[i - 1].position.Y) / run;
            if (grade > maxGrade) {
                maxGrade = grade;
                maxGradeRoad = road.spec->id;
            }
        }
        if (road.pieces.empty()) {
            std::cout << "error: road '" << road.spec->id << "' produced no drivable piece\n";
            ++problems;
        }
    }
    if (maxGrade > 0.12f) {
        std::cout << "warning: road '" << maxGradeRoad << "' reaches a grade of " << maxGrade * 100.0f << " %\n";
    }
    std::size_t deadEnds = 0;
    for (const auto& lane : lanes.Lanes()) {
        if (lane.outgoingLinks.empty()) {
            ++deadEnds;
            std::cout << "warning: lane " << lane.id << " on road '" << roads.Roads()[static_cast<std::size_t>(lane.road)].spec->id
                      << "' has no continuation\n";
        }
        if (lane.length < 5.0f) {
            std::cout << "warning: lane " << lane.id << " on road '" << roads.Roads()[static_cast<std::size_t>(lane.road)].spec->id
                      << "' is only " << lane.length << " m long\n";
        }
    }
    if (!lanes.Lanes().empty()) {
        const auto reachable = lanes.Reachable(0);
        if (reachable.size() != lanes.Lanes().size()) {
            std::cout << "error: only " << reachable.size() << " of " << lanes.Lanes().size() << " lanes are reachable from lane 0\n";
            ++problems;
        }
    }
    for (const auto& spawn : world->Data().traffic.playerSpawns) {
        const float heading = spawn.headingDeg * 3.14159265f / 180.0f;
        const int lane = lanes.NearestLane(spawn.position, heading, 6.0f);
        if (lane < 0) {
            std::cout << "warning: player spawn '" << spawn.name << "' is more than 6 m from any lane\n";
        }
    }

    if (!quiet) {
        std::printf("map: %s (%s)\n", world->Data().info.displayName.c_str(), world->Data().info.id.c_str());
        std::printf("terrain: %d x %d vertices, cell %.1f m, heights %.1f..%.1f m\n", world->Terrain().Columns(), world->Terrain().Rows(),
                    static_cast<double>(world->Terrain().CellSize()), static_cast<double>(world->Terrain().MinHeight()),
                    static_cast<double>(world->Terrain().MaxHeight()));
        std::printf("roads: %zu, total length %.2f km, intersections %zu, pieces %zu\n", roads.Roads().size(),
                    static_cast<double>(roads.TotalLength() / 1000.0f), roads.Intersections().size(), roads.Pieces().size());
        std::printf("lanes: %zu (%.2f km), links %zu, dead ends %zu, max grade %.1f %%\n", lanes.Lanes().size(),
                    static_cast<double>(lanes.TotalLaneLength() / 1000.0f), lanes.Links().size(), deadEnds, static_cast<double>(maxGrade * 100.0f));
        for (const auto& inter : roads.Intersections()) {
            const auto& node = world->Data().nodes[static_cast<std::size_t>(inter.node)];
            std::printf("  node %-8s approaches %zu:", node.id.c_str(), inter.approaches.size());
            for (const auto& a : inter.approaches) {
                std::printf(" %s(%s, setback %.1f)", roads.Roads()[static_cast<std::size_t>(a.road)].spec->id.c_str(), Map::ToString(a.control), static_cast<double>(a.setback));
            }
            std::printf("\n");
        }
        std::printf("objects: %zu buildings, %zu props, %zu signs, %zu trees, %zu forests, %zu avenues, %zu parked cars\n",
                    world->Data().objects.buildings.size(), world->Data().objects.props.size(), world->Data().objects.signs.size(),
                    world->Data().objects.trees.size(), world->Data().objects.forests.size(), world->Data().objects.avenues.size(),
                    world->Data().objects.vehicles.size());
        std::printf("placed: %zu buildings, %zu trees, %zu signs, %zu props (incl. delineators), %zu parked cars\n", world->Objects().Buildings().size(),
                    world->Objects().Trees().size(), world->Objects().Signs().size(), world->Objects().Props().size(),
                    world->Objects().Vehicles().size());
        std::printf("build times: load %.2f s (roads %.2f s, terrain %.2f s, lanes %.2f s, objects %.2f s)\n", world->Stats().loadSeconds,
                    world->Stats().roadSeconds, world->Stats().terrainSeconds, world->Stats().laneSeconds, world->Stats().objectSeconds);
    }
    if (problems > 0) {
        std::printf("map-validate: FAILED (%d problem(s))\n", problems);
        return 1;
    }
    std::printf("map-validate: OK (%zu warning(s))\n", warnings.size());
    return 0;
}
