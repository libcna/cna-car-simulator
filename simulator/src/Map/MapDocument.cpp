#include "CarSim/Map/MapDocument.hpp"

#include "CarSim/Core/JsonReader.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace CarSim::Map
{
    using Core::JsonReader;
    using Microsoft::Xna::Framework::Vector2;
    using System::Text::Json::JsonElement;

    namespace
    {
        constexpr const char* kKnownSignCodes[] = {
            "P1", "P2", "P3", "P4", "P6", "B20a", "B20b", "B21a", "B21b", "IZ4a", "IZ4b", "IS3a", "IS3b", "IS3c", "IS3d",
            "IP6", "IJ4c", "A7a", "A12a", "A14", "A22", "Z11a", "Z11b", "B1", "B2", "C2a"};

        bool CheckSchemaVersion(const JsonReader& r, const JsonElement& root, const std::string& file, std::vector<std::string>& errors)
        {
            if (!JsonReader::IsObject(root)) {
                errors.push_back(file + ": root must be an object");
                return false;
            }
            int version = -1;
            r.Int(root, "schemaVersion", version, file, true);
            if (version < 0) {
                return false;
            }
            if (version > kMapSchemaVersion) {
                errors.push_back(file + ": schemaVersion " + std::to_string(version) + " is newer than the supported version " +
                                 std::to_string(kMapSchemaVersion));
                return false;
            }
            if (version < 1) {
                errors.push_back(file + ": schemaVersion must be at least 1");
                return false;
            }
            // Version 1 is the only published version; in-memory upgraders for later versions go here.
            return true;
        }

        unsigned SeedOf(const JsonReader& r, const JsonElement& obj, const std::string& path, unsigned fallback)
        {
            int seed = static_cast<int>(fallback);
            r.Int(obj, "seed", seed, path);
            return static_cast<unsigned>(std::max(0, seed));
        }

        void ParseTerrain(const JsonReader& r, const JsonElement& root, TerrainSpec& t)
        {
            const std::string p = "terrain";
            Vector2 size(t.sizeX, t.sizeZ);
            r.Vec2(root, "size", size, p);
            t.sizeX = size.X;
            t.sizeZ = size.Y;
            r.Float(root, "cellSize", t.cellSize, p);
            r.Float(root, "baseHeight", t.baseHeight, p);
            r.Float(root, "roadBlendWidth", t.roadBlendWidth, p);
            JsonElement noise;
            if (r.HasObject(root, "noise", noise)) {
                r.Float(noise, "amplitude", t.noiseAmplitude, p + ".noise");
                r.Float(noise, "wavelength", t.noiseWavelength, p + ".noise");
                r.Int(noise, "octaves", t.noiseOctaves, p + ".noise");
                t.seed = SeedOf(r, noise, p + ".noise", t.seed);
            }
            JsonElement features;
            if (r.HasArray(root, "features", features)) {
                std::size_t i = 0;
                for (const auto& f : features.EnumerateArray()) {
                    const std::string fp = p + ".features[" + std::to_string(i++) + "]";
                    TerrainFeatureSpec spec;
                    std::string type = "hill";
                    r.String(f, "type", type, fp);
                    if (type == "hill") spec.type = TerrainFeatureType::Hill;
                    else if (type == "ridge") spec.type = TerrainFeatureType::Ridge;
                    else if (type == "plateau") spec.type = TerrainFeatureType::Plateau;
                    else r.Error(fp + ".type: unknown feature type '" + type + "'");
                    r.Vec2(f, "center", spec.center, fp, true);
                    spec.end = spec.center;
                    r.Vec2(f, "end", spec.end, fp);
                    r.Float(f, "radius", spec.radius, fp);
                    r.Float(f, "height", spec.height, fp);
                    t.features.push_back(spec);
                }
            }
            JsonElement regions;
            if (r.HasArray(root, "regions", regions)) {
                std::size_t i = 0;
                for (const auto& e : regions.EnumerateArray()) {
                    const std::string rp = p + ".regions[" + std::to_string(i++) + "]";
                    RegionSpec spec;
                    std::string type = "meadow";
                    r.String(e, "type", type, rp, true);
                    if (!ParseRegionType(type, spec.type)) {
                        r.Error(rp + ".type: unknown region type '" + type + "'");
                    }
                    r.Vec2Array(e, "polygon", spec.polygon, rp, true);
                    r.String(e, "crop", spec.crop, rp);
                    spec.seed = SeedOf(r, e, rp, static_cast<unsigned>(i));
                    t.regions.push_back(std::move(spec));
                }
            }
        }

        void ParseRoads(const JsonReader& r, const JsonElement& root, MapData& data)
        {
            JsonElement nodes = r.RequireArray(root, "nodes", "roads");
            if (JsonReader::IsArray(nodes)) {
                std::size_t i = 0;
                for (const auto& e : nodes.EnumerateArray()) {
                    const std::string np = "roads.nodes[" + std::to_string(i++) + "]";
                    RoadNodeSpec n;
                    r.String(e, "id", n.id, np, true);
                    r.Vec2(e, "position", n.position, np, true);
                    if (r.Has(e, "elevation")) {
                        float elevation = 0.0f;
                        r.Float(e, "elevation", elevation, np);
                        n.elevation = elevation;
                    }
                    r.Bool(e, "urban", n.urban, np);
                    r.String(e, "name", n.name, np);
                    r.StringArray(e, "mainRoads", n.mainRoads, np);
                    r.Float(e, "cornerRadius", n.cornerRadius, np);
                    JsonElement controls;
                    if (r.HasArray(e, "control", controls)) {
                        std::size_t j = 0;
                        for (const auto& c : controls.EnumerateArray()) {
                            const std::string cp = np + ".control[" + std::to_string(j++) + "]";
                            std::string road;
                            std::string control;
                            r.String(c, "road", road, cp, true);
                            r.String(c, "control", control, cp, true);
                            ApproachControl parsed = ApproachControl::Yield;
                            if (!ParseApproachControl(control, parsed)) {
                                r.Error(cp + ".control: unknown control '" + control + "'");
                            }
                            n.approachControl[road] = parsed;
                        }
                    }
                    JsonElement signals;
                    if (r.HasObject(e, "signals", signals)) {
                        const std::string sp = np + ".signals";
                        n.signals.enabled = true;
                        r.Bool(signals, "enabled", n.signals.enabled, sp);
                        r.Float(signals, "green", n.signals.greenSeconds, sp);
                        r.Float(signals, "amber", n.signals.amberSeconds, sp);
                        r.Float(signals, "allRed", n.signals.allRedSeconds, sp);
                        r.Float(signals, "offset", n.signals.offsetSeconds, sp);
                        JsonElement groups;
                        if (r.HasArray(signals, "groups", groups)) {
                            std::size_t g = 0;
                            for (const auto& group : groups.EnumerateArray()) {
                                std::vector<std::string> roads;
                                if (JsonReader::IsArray(group)) {
                                    for (const auto& road : group.EnumerateArray()) {
                                        if (JsonReader::IsString(road)) roads.push_back(road.GetString());
                                    }
                                }
                                if (roads.empty()) {
                                    r.Error(sp + ".groups[" + std::to_string(g) + "]: must list at least one road");
                                }
                                n.signals.groups.push_back(std::move(roads));
                                ++g;
                            }
                        }
                    }
                    data.nodes.push_back(std::move(n));
                }
            }
            JsonElement roads = r.RequireArray(root, "roads", "roads");
            if (JsonReader::IsArray(roads)) {
                std::size_t i = 0;
                for (const auto& e : roads.EnumerateArray()) {
                    const std::string rp = "roads.roads[" + std::to_string(i++) + "]";
                    RoadSpec road;
                    r.String(e, "id", road.id, rp, true);
                    r.String(e, "name", road.name, rp);
                    r.String(e, "number", road.number, rp);
                    std::string cls = "III";
                    r.String(e, "class", cls, rp);
                    if (!ParseRoadClass(cls, road.roadClass)) {
                        r.Error(rp + ".class: unknown road class '" + cls + "'");
                    }
                    r.StringArray(e, "nodes", road.nodes, rp);
                    r.Int(e, "lanesPerDirection", road.lanesPerDirection, rp);
                    r.Float(e, "laneWidth", road.laneWidth, rp);
                    r.Float(e, "edgeStripWidth", road.edgeStripWidth, rp);
                    r.Float(e, "shoulderWidth", road.shoulderWidth, rp);
                    std::string surface = "asphalt";
                    r.String(e, "surface", surface, rp);
                    if (!ParseSurface(surface, road.surface)) {
                        r.Error(rp + ".surface: unknown surface '" + surface + "'");
                    }
                    r.Float(e, "speedLimitKmh", road.speedLimitKmh, rp);
                    r.Float(e, "urbanSpeedLimitKmh", road.urbanSpeedLimitKmh, rp);
                    std::string centre = "dashed";
                    r.String(e, "centreLine", centre, rp);
                    if (centre == "none") road.centreLine = CentreLineMarking::None;
                    else if (centre == "solid") road.centreLine = CentreLineMarking::Solid;
                    else if (centre == "dashed") road.centreLine = CentreLineMarking::Dashed;
                    else if (centre == "solid-forward") road.centreLine = CentreLineMarking::SolidForward;
                    else if (centre == "solid-reverse") road.centreLine = CentreLineMarking::SolidReverse;
                    else r.Error(rp + ".centreLine: expected none|solid|dashed|solid-forward|solid-reverse");
                    r.Bool(e, "noOvertaking", road.noOvertaking, rp);
                    JsonElement sections;
                    if (r.HasArray(e, "centreLineSections", sections)) {
                        float previousEnd = 0.0f;
                        std::size_t si = 0;
                        for (const auto& item : sections.EnumerateArray()) {
                            const std::string sp = rp + ".centreLineSections[" + std::to_string(si++) + "]";
                            CentreLineSection section;
                            section.marking = road.centreLine;
                            r.Float(item, "fromM", section.fromM, sp);
                            r.Float(item, "toM", section.toM, sp);
                            std::string marking = centre;
                            r.String(item, "centreLine", marking, sp);
                            if (marking == "none") section.marking = CentreLineMarking::None;
                            else if (marking == "solid") section.marking = CentreLineMarking::Solid;
                            else if (marking == "dashed") section.marking = CentreLineMarking::Dashed;
                            else if (marking == "solid-forward") section.marking = CentreLineMarking::SolidForward;
                            else if (marking == "solid-reverse") section.marking = CentreLineMarking::SolidReverse;
                            else r.Error(sp + ".centreLine: expected none|solid|dashed|solid-forward|solid-reverse");
                            r.Bool(item, "noOvertaking", section.noOvertaking, sp);
                            r.Bool(item, "noOvertakingForward", section.noOvertakingForward, sp);
                            r.Bool(item, "noOvertakingReverse", section.noOvertakingReverse, sp);
                            if (section.fromM < previousEnd || section.fromM < 0.0f || section.toM <= section.fromM) {
                                r.Error(sp + ": expected ordered, non-overlapping fromM < toM in road metres");
                            }
                            previousEnd = section.toM;
                            road.centreLineSections.push_back(section);
                        }
                    }
                    r.Bool(e, "edgeLines", road.edgeLines, rp);
                    JsonElement sidewalk;
                    if (r.HasObject(e, "sidewalk", sidewalk)) {
                        r.Float(sidewalk, "width", road.sidewalk.width, rp + ".sidewalk");
                        r.Bool(sidewalk, "left", road.sidewalk.left, rp + ".sidewalk");
                        r.Bool(sidewalk, "right", road.sidewalk.right, rp + ".sidewalk");
                        r.Float(sidewalk, "kerbHeight", road.sidewalk.kerbHeight, rp + ".sidewalk");
                    }
                    r.Float(e, "cornerRadius", road.cornerRadius, rp);
                    r.Bool(e, "oneWay", road.oneWay, rp);
                    data.roads.push_back(std::move(road));
                }
            }
        }

        void ParseObjects(const JsonReader& r, const JsonElement& root, ObjectsSpec& o)
        {
            JsonElement arr;
            if (r.HasArray(root, "buildings", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.buildings[" + std::to_string(i++) + "]";
                    BuildingSpec b;
                    r.String(e, "type", b.type, p);
                    r.Vec2(e, "position", b.position, p, true);
                    r.Float(e, "rotationDeg", b.rotationDeg, p);
                    r.Float(e, "width", b.width, p);
                    r.Float(e, "depth", b.depth, p);
                    r.Float(e, "eavesHeight", b.eavesHeight, p);
                    r.Float(e, "roofPitchDeg", b.roofPitchDeg, p);
                    r.Int(e, "floors", b.floors, p);
                    r.String(e, "wallColor", b.wallColor, p);
                    r.String(e, "roofColor", b.roofColor, p);
                    b.seed = SeedOf(r, e, p, static_cast<unsigned>(i));
                    o.buildings.push_back(std::move(b));
                }
            }
            if (r.HasArray(root, "props", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.props[" + std::to_string(i++) + "]";
                    PropSpec s;
                    r.String(e, "type", s.type, p, true);
                    r.Vec2(e, "position", s.position, p, true);
                    r.Float(e, "rotationDeg", s.rotationDeg, p);
                    r.Float(e, "length", s.length, p);
                    r.Float(e, "scale", s.scale, p);
                    o.props.push_back(std::move(s));
                }
            }
            if (r.HasArray(root, "signs", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.signs[" + std::to_string(i++) + "]";
                    SignSpec s;
                    r.String(e, "code", s.code, p, true);
                    r.Vec2(e, "position", s.position, p, true);
                    r.Float(e, "headingDeg", s.headingDeg, p);
                    r.String(e, "road", s.roadId, p);
                    r.String(e, "text", s.text, p);
                    r.Float(e, "value", s.value, p);
                    o.signs.push_back(std::move(s));
                }
            }
            if (r.HasArray(root, "vehicles", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.vehicles[" + std::to_string(i++) + "]";
                    VehicleSpec v;
                    r.String(e, "body", v.body, p);
                    r.Vec2(e, "position", v.position, p, true);
                    r.Float(e, "rotationDeg", v.rotationDeg, p);
                    v.seed = SeedOf(r, e, p, static_cast<unsigned>(i));
                    o.vehicles.push_back(std::move(v));
                }
            }
            if (r.HasArray(root, "trees", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.trees[" + std::to_string(i++) + "]";
                    TreeSpec t;
                    r.String(e, "species", t.species, p);
                    r.Vec2(e, "position", t.position, p, true);
                    r.Float(e, "scale", t.scale, p);
                    t.seed = SeedOf(r, e, p, static_cast<unsigned>(i));
                    o.trees.push_back(std::move(t));
                }
            }
            if (r.HasArray(root, "forests", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.forests[" + std::to_string(i++) + "]";
                    ForestSpec f;
                    r.Vec2Array(e, "polygon", f.polygon, p, true);
                    r.Float(e, "density", f.density, p);
                    r.Float(e, "margin", f.margin, p);
                    f.seed = SeedOf(r, e, p, static_cast<unsigned>(i));
                    JsonElement species;
                    if (r.HasArray(e, "species", species)) {
                        std::size_t j = 0;
                        for (const auto& s : species.EnumerateArray()) {
                            const std::string sp = p + ".species[" + std::to_string(j++) + "]";
                            std::string name;
                            float weight = 1.0f;
                            r.String(s, "species", name, sp, true);
                            r.Float(s, "weight", weight, sp);
                            if (!name.empty()) {
                                f.species[name] = weight;
                            }
                        }
                    }
                    if (f.species.empty()) {
                        f.species["spruce"] = 1.0f;
                    }
                    o.forests.push_back(std::move(f));
                }
            }
            if (r.HasArray(root, "avenues", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "objects.avenues[" + std::to_string(i++) + "]";
                    AvenueSpec a;
                    r.String(e, "road", a.road, p, true);
                    r.String(e, "species", a.species, p);
                    r.Float(e, "spacing", a.spacing, p);
                    r.Float(e, "offset", a.offset, p);
                    r.Bool(e, "left", a.left, p);
                    r.Bool(e, "right", a.right, p);
                    r.String(e, "fromNode", a.fromNode, p);
                    r.String(e, "toNode", a.toNode, p);
                    a.seed = SeedOf(r, e, p, static_cast<unsigned>(i));
                    o.avenues.push_back(std::move(a));
                }
            }
        }

        void ParseTraffic(const JsonReader& r, const JsonElement& root, TrafficSpec& t)
        {
            JsonElement arr;
            if (r.HasArray(root, "playerSpawns", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "traffic.playerSpawns[" + std::to_string(i++) + "]";
                    SpawnSpec s;
                    r.String(e, "name", s.name, p);
                    r.Vec2(e, "position", s.position, p, true);
                    r.Float(e, "headingDeg", s.headingDeg, p);
                    t.playerSpawns.push_back(std::move(s));
                }
            }
            if (r.HasArray(root, "routes", arr)) {
                std::size_t i = 0;
                for (const auto& e : arr.EnumerateArray()) {
                    const std::string p = "traffic.routes[" + std::to_string(i++) + "]";
                    RouteSpec route;
                    r.String(e, "name", route.name, p);
                    r.String(e, "spawn", route.spawn, p);
                    r.String(e, "description", route.description, p);
                    JsonElement points;
                    if (r.HasArray(e, "waypoints", points)) {
                        std::size_t j = 0;
                        for (const auto& point : points.EnumerateArray()) {
                            Microsoft::Xna::Framework::Vector2 wp;
                            if (r.Vec2Value(point, wp, p + ".waypoints[" + std::to_string(j++) + "]")) {
                                route.waypoints.push_back(wp);
                            }
                        }
                    }
                    t.routes.push_back(std::move(route));
                }
            }
            r.Float(root, "densityPerKm", t.densityPerKm, "traffic");
            r.Int(root, "maxVehicles", t.maxVehicles, "traffic");
            r.StringArray(root, "vehicles", t.vehicles, "traffic");
            r.Float(root, "spawnMinDistance", t.spawnMinDistance, "traffic");
            r.Float(root, "despawnDistance", t.despawnDistance, "traffic");
        }
    }

    std::string MapDirectory(const std::string& contentRoot, const std::string& mapName)
    {
        return (std::filesystem::path(contentRoot) / "maps" / mapName).string();
    }

    MapLoadResult ParseMapSources(const MapSourceTexts& sources)
    {
        MapLoadResult result;
        auto& errors = result.errors;
        JsonReader r(errors);

        const auto mapDoc = Core::ParseJsonText(sources.map, "map.json", errors);
        if (!mapDoc) {
            return result;
        }
        const JsonElement mapRoot = mapDoc->getRootElementProperty();
        if (!CheckSchemaVersion(r, mapRoot, "map.json", errors)) {
            return result;
        }
        auto& info = result.data.info;
        r.Int(mapRoot, "schemaVersion", info.schemaVersion, "map");
        r.String(mapRoot, "id", info.id, "map", true);
        r.String(mapRoot, "displayName", info.displayName, "map");
        r.String(mapRoot, "description", info.description, "map");
        r.String(mapRoot, "author", info.author, "map");
        r.String(mapRoot, "license", info.license, "map");
        if (info.displayName.empty()) {
            info.displayName = info.id;
        }

        if (sources.terrain.empty()) {
            errors.push_back("terrain.json: missing");
        } else if (const auto doc = Core::ParseJsonText(sources.terrain, "terrain.json", errors)) {
            const JsonElement root = doc->getRootElementProperty();
            if (CheckSchemaVersion(r, root, "terrain.json", errors)) {
                ParseTerrain(r, root, result.data.terrain);
            }
        }
        if (sources.roads.empty()) {
            errors.push_back("roads.json: missing");
        } else if (const auto doc = Core::ParseJsonText(sources.roads, "roads.json", errors)) {
            const JsonElement root = doc->getRootElementProperty();
            if (CheckSchemaVersion(r, root, "roads.json", errors)) {
                ParseRoads(r, root, result.data);
            }
        }
        if (!sources.objects.empty()) {
            if (const auto doc = Core::ParseJsonText(sources.objects, "objects.json", errors)) {
                const JsonElement root = doc->getRootElementProperty();
                if (CheckSchemaVersion(r, root, "objects.json", errors)) {
                    ParseObjects(r, root, result.data.objects);
                }
            }
        }
        if (!sources.traffic.empty()) {
            if (const auto doc = Core::ParseJsonText(sources.traffic, "traffic.json", errors)) {
                const JsonElement root = doc->getRootElementProperty();
                if (CheckSchemaVersion(r, root, "traffic.json", errors)) {
                    ParseTraffic(r, root, result.data.traffic);
                }
            }
        }
        if (errors.empty()) {
            ValidateMapData(result.data, result.errors, result.warnings);
        }
        return result;
    }

    MapLoadResult LoadMapDirectory(const std::string& directory)
    {
        namespace fs = std::filesystem;
        MapLoadResult result;
        MapSourceTexts texts;
        const fs::path dir(directory);
        if (!Core::ReadTextFile((dir / "map.json").string(), texts.map, result.errors)) {
            return result;
        }
        // The map file may rename the companion files; read those names before parsing fully.
        std::string terrainFile = "terrain.json";
        std::string roadsFile = "roads.json";
        std::string objectsFile = "objects.json";
        std::string trafficFile = "traffic.json";
        {
            std::vector<std::string> ignored;
            if (const auto doc = Core::ParseJsonText(texts.map, "map.json", ignored)) {
                JsonReader r(ignored);
                JsonElement files;
                if (r.HasObject(doc->getRootElementProperty(), "files", files)) {
                    r.String(files, "terrain", terrainFile, "map.files");
                    r.String(files, "roads", roadsFile, "map.files");
                    r.String(files, "objects", objectsFile, "map.files");
                    r.String(files, "traffic", trafficFile, "map.files");
                }
            }
        }
        std::error_code ec;
        if (fs::exists(dir / terrainFile, ec)) {
            Core::ReadTextFile((dir / terrainFile).string(), texts.terrain, result.errors);
        }
        if (fs::exists(dir / roadsFile, ec)) {
            Core::ReadTextFile((dir / roadsFile).string(), texts.roads, result.errors);
        }
        if (fs::exists(dir / objectsFile, ec)) {
            Core::ReadTextFile((dir / objectsFile).string(), texts.objects, result.errors);
        }
        if (fs::exists(dir / trafficFile, ec)) {
            Core::ReadTextFile((dir / trafficFile).string(), texts.traffic, result.errors);
        }
        if (!result.errors.empty()) {
            return result;
        }
        MapLoadResult parsed = ParseMapSources(texts);
        return parsed;
    }

    void ValidateMapData(const MapData& data, std::vector<std::string>& errors, std::vector<std::string>& warnings)
    {
        const auto& t = data.terrain;
        if (t.sizeX < 200.0f || t.sizeZ < 200.0f) {
            errors.push_back("terrain.size: the map must be at least 200 x 200 m");
        }
        if (t.cellSize < 1.0f || t.cellSize > 10.0f) {
            errors.push_back("terrain.cellSize: must be between 1 and 10 m");
        }
        for (std::size_t i = 0; i < t.features.size(); ++i) {
            if (t.features[i].radius <= 1.0f) {
                errors.push_back("terrain.features[" + std::to_string(i) + "].radius: must be > 1 m");
            }
        }
        for (std::size_t i = 0; i < t.regions.size(); ++i) {
            if (t.regions[i].polygon.size() < 3) {
                errors.push_back("terrain.regions[" + std::to_string(i) + "].polygon: needs at least three points");
            }
        }
        const float halfX = t.sizeX * 0.5f;
        const float halfZ = t.sizeZ * 0.5f;
        const auto inside = [&](const Vector2& p) { return std::fabs(p.X) <= halfX && std::fabs(p.Y) <= halfZ; };

        std::set<std::string> nodeIds;
        for (const auto& n : data.nodes) {
            if (n.id.empty()) {
                errors.push_back("roads.nodes: every node needs an id");
                continue;
            }
            if (!nodeIds.insert(n.id).second) {
                errors.push_back("roads.nodes: duplicate node id '" + n.id + "'");
            }
            if (!inside(n.position)) {
                errors.push_back("roads.nodes['" + n.id + "']: position lies outside the terrain");
            }
        }
        std::set<std::string> roadIds;
        std::map<std::string, int> nodeUse;
        for (const auto& road : data.roads) {
            const std::string rp = "roads.roads['" + road.id + "']";
            if (road.id.empty()) {
                errors.push_back("roads.roads: every road needs an id");
                continue;
            }
            if (!roadIds.insert(road.id).second) {
                errors.push_back(rp + ": duplicate road id");
            }
            if (road.nodes.size() < 2) {
                errors.push_back(rp + ".nodes: a road needs at least two nodes");
            }
            for (std::size_t i = 0; i < road.nodes.size(); ++i) {
                const auto* node = data.FindNode(road.nodes[i]);
                if (!node) {
                    errors.push_back(rp + ".nodes[" + std::to_string(i) + "]: unknown node '" + road.nodes[i] + "'");
                    continue;
                }
                ++nodeUse[node->id];
                if (i > 0) {
                    if (const auto* prev = data.FindNode(road.nodes[i - 1])) {
                        const float d = Vector2::Distance(prev->position, node->position);
                        if (d < 4.0f) {
                            errors.push_back(rp + ".nodes[" + std::to_string(i) + "]: consecutive nodes closer than 4 m");
                        }
                    }
                }
            }
            const bool narrowClass = road.roadClass == RoadClass::Track || road.roadClass == RoadClass::Forest;
            if (!narrowClass && (road.laneWidth < 2.2f || road.laneWidth > 4.5f)) {
                warnings.push_back(rp + ".laneWidth " + std::to_string(road.laneWidth) + " m is outside the usual 2.5-3.75 m");
            }
            if (road.lanesPerDirection < 1 || road.lanesPerDirection > 3) {
                errors.push_back(rp + ".lanesPerDirection: must be 1..3");
            }
            if (road.speedLimitKmh < 5.0f || road.speedLimitKmh > 130.0f) {
                errors.push_back(rp + ".speedLimitKmh: must be 5..130");
            }
            if (road.sidewalk.width > 0.0f && !road.sidewalk.left && !road.sidewalk.right) {
                warnings.push_back(rp + ".sidewalk: width given but neither side enabled");
            }
        }
        for (const auto& n : data.nodes) {
            for (const auto& main : n.mainRoads) {
                const auto* road = data.FindRoad(main);
                if (!road) {
                    errors.push_back("roads.nodes['" + n.id + "'].mainRoads: unknown road '" + main + "'");
                } else if (std::find(road->nodes.begin(), road->nodes.end(), n.id) == road->nodes.end()) {
                    errors.push_back("roads.nodes['" + n.id + "'].mainRoads: road '" + main + "' does not pass through this node");
                }
            }
            for (const auto& [roadId, control] : n.approachControl) {
                const auto* road = data.FindRoad(roadId);
                if (!road) {
                    errors.push_back("roads.nodes['" + n.id + "'].control: unknown road '" + roadId + "'");
                } else if (std::find(road->nodes.begin(), road->nodes.end(), n.id) == road->nodes.end()) {
                    errors.push_back("roads.nodes['" + n.id + "'].control: road '" + roadId + "' does not pass through this node");
                }
            }
            if (n.signals.enabled) {
                const std::string sp = "roads.nodes['" + n.id + "'].signals";
                if (n.signals.greenSeconds < 3.0f) {
                    errors.push_back(sp + ".green: must be at least 3 s");
                }
                if (n.signals.amberSeconds < 0.0f || n.signals.allRedSeconds < 0.0f) {
                    errors.push_back(sp + ": amber and allRed must not be negative");
                }
                if (n.signals.groups.size() == 1) {
                    warnings.push_back(sp + ".groups: a single group is always green; give the side roads a group too");
                }
                std::vector<std::string> seen;
                for (const auto& group : n.signals.groups) {
                    for (const auto& roadId : group) {
                        const auto* road = data.FindRoad(roadId);
                        if (!road) {
                            errors.push_back(sp + ".groups: unknown road '" + roadId + "'");
                        } else if (std::find(road->nodes.begin(), road->nodes.end(), n.id) == road->nodes.end()) {
                            errors.push_back(sp + ".groups: road '" + roadId + "' does not pass through this node");
                        }
                        if (std::find(seen.begin(), seen.end(), roadId) != seen.end()) {
                            errors.push_back(sp + ".groups: road '" + roadId + "' appears in more than one group");
                        }
                        seen.push_back(roadId);
                    }
                }
            }
            if (nodeUse[n.id] == 0) {
                warnings.push_back("roads.nodes['" + n.id + "'] is not used by any road");
            }
        }

        const auto& o = data.objects;
        for (std::size_t i = 0; i < o.buildings.size(); ++i) {
            const auto& b = o.buildings[i];
            if (b.width <= 1.0f || b.depth <= 1.0f || b.eavesHeight <= 1.0f) {
                errors.push_back("objects.buildings[" + std::to_string(i) + "]: width, depth and eavesHeight must exceed 1 m");
            }
            if (!inside(b.position)) {
                errors.push_back("objects.buildings[" + std::to_string(i) + "]: position lies outside the terrain");
            }
        }
        for (std::size_t i = 0; i < o.signs.size(); ++i) {
            const auto& s = o.signs[i];
            const bool known = std::any_of(std::begin(kKnownSignCodes), std::end(kKnownSignCodes), [&](const char* c) { return s.code == c; });
            if (!known) {
                warnings.push_back("objects.signs[" + std::to_string(i) + "]: sign code '" + s.code + "' has no generator yet");
            }
            if (!inside(s.position)) {
                errors.push_back("objects.signs[" + std::to_string(i) + "]: position lies outside the terrain");
            }
            if (!s.roadId.empty() && !data.FindRoad(s.roadId)) {
                errors.push_back("objects.signs[" + std::to_string(i) + "]: unknown road '" + s.roadId + "'");
            }
        }
        for (std::size_t i = 0; i < o.forests.size(); ++i) {
            if (o.forests[i].polygon.size() < 3) {
                errors.push_back("objects.forests[" + std::to_string(i) + "].polygon: needs at least three points");
            }
            if (o.forests[i].density <= 0.0f || o.forests[i].density > 0.2f) {
                errors.push_back("objects.forests[" + std::to_string(i) + "].density: must be in (0, 0.2] trees per m^2");
            }
        }
        for (std::size_t i = 0; i < o.avenues.size(); ++i) {
            if (!data.FindRoad(o.avenues[i].road)) {
                errors.push_back("objects.avenues[" + std::to_string(i) + "].road: unknown road '" + o.avenues[i].road + "'");
            }
            if (o.avenues[i].spacing < 4.0f) {
                errors.push_back("objects.avenues[" + std::to_string(i) + "].spacing: must be at least 4 m");
            }
        }
        for (std::size_t i = 0; i < o.trees.size(); ++i) {
            if (!inside(o.trees[i].position)) {
                errors.push_back("objects.trees[" + std::to_string(i) + "]: position lies outside the terrain");
            }
        }

        const auto& tr = data.traffic;
        if (tr.playerSpawns.empty()) {
            warnings.push_back("traffic.playerSpawns: no player spawn; the first lane start will be used");
        }
        for (std::size_t i = 0; i < tr.playerSpawns.size(); ++i) {
            if (!inside(tr.playerSpawns[i].position)) {
                errors.push_back("traffic.playerSpawns[" + std::to_string(i) + "]: position lies outside the terrain");
            }
        }
        if (tr.maxVehicles < 0 || tr.maxVehicles > 200) {
            errors.push_back("traffic.maxVehicles: must be 0..200");
        }
    }
}
