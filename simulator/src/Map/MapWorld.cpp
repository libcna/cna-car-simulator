#include "CarSim/Map/MapWorld.hpp"

#include "CarSim/Map/MapDocument.hpp"

#include <chrono>
#include <cmath>
#include <numbers>

namespace CarSim::Map
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;

    float HeadingFromDirection(const float dx, const float dz)
    {
        return std::atan2(dx, -dz);
    }

    Vector2 DirectionFromHeading(const float headingRad)
    {
        return Vector2(std::sin(headingRad), -std::cos(headingRad));
    }

    // ------------------------------------------------------------------ MapGround

    SurfaceSample MapGround::Sample(const float x, const float z) const
    {
        SurfaceSample s;
        float height = 0.0f;
        Sim::SurfaceType surface = Sim::SurfaceType::Asphalt;
        float edge = 0.0f;
        if (roads_.RoadSurfaceAt(Vector2(x, z), height, surface, edge)) {
            s.height = height;
            s.surface = surface;
            s.onRoad = true;
            s.distanceToPavedEdge = edge;
            return s;
        }
        s.height = terrain_.Height(x, z);
        switch (terrain_.RegionAt(x, z)) {
            case RegionType::Field: s.surface = Sim::SurfaceType::Dirt; break;
            case RegionType::Forest: s.surface = Sim::SurfaceType::Dirt; break;
            case RegionType::Square: s.surface = Sim::SurfaceType::Cobbles; break;
            case RegionType::Yard: s.surface = Sim::SurfaceType::Concrete; break;
            default: s.surface = Sim::SurfaceType::Grass; break;
        }
        s.onRoad = false;
        s.distanceToPavedEdge = 1e6f;
        return s;
    }

    Vector3 MapGround::NormalAt(const float x, const float z) const
    {
        const float d = 0.35f;
        const float hx = Sample(x + d, z).height - Sample(x - d, z).height;
        const float hz = Sample(x, z + d).height - Sample(x, z - d).height;
        Vector3 n(-hx, 2.0f * d, -hz);
        n.Normalize();
        return n;
    }

    bool MapGround::Raycast(const Vector3& origin, const Vector3& direction, const float maxDistance, Sim::GroundHit& hit) const
    {
        const auto above = [&](const float t) {
            const Vector3 p = origin + direction * t;
            return p.Y - Sample(p.X, p.Z).height;
        };
        float t0 = 0.0f;
        float f0 = above(0.0f);
        if (f0 <= 0.0f) {
            // Already below the surface: report a hit at the origin so the suspension can push out.
            const SurfaceSample s = Sample(origin.X, origin.Z);
            hit.point = Vector3(origin.X, s.height, origin.Z);
            hit.normal = NormalAt(origin.X, origin.Z);
            hit.distance = 0.0f;
            hit.surface = s.surface;
            return true;
        }
        const float step = 0.25f;
        float t1 = step;
        bool crossed = false;
        while (t1 <= maxDistance + 1e-4f) {
            const float f1 = above(t1);
            if (f1 <= 0.0f) {
                crossed = true;
                break;
            }
            t0 = t1;
            f0 = f1;
            t1 += step;
        }
        if (!crossed) {
            const float f1 = above(maxDistance);
            if (f1 > 0.0f) {
                return false;
            }
            t1 = maxDistance;
        }
        for (int i = 0; i < 10; ++i) {
            const float tm = 0.5f * (t0 + t1);
            if (above(tm) > 0.0f) {
                t0 = tm;
            } else {
                t1 = tm;
            }
        }
        const float t = 0.5f * (t0 + t1);
        const Vector3 p = origin + direction * t;
        const SurfaceSample s = Sample(p.X, p.Z);
        hit.point = Vector3(p.X, s.height, p.Z);
        hit.normal = NormalAt(p.X, p.Z);
        hit.distance = t;
        hit.surface = s.surface;
        return true;
    }

    // ------------------------------------------------------------------ MapWorld

    std::unique_ptr<MapWorld> MapWorld::Load(const std::string& directory, std::vector<std::string>& errors, std::vector<std::string>* warnings)
    {
        const auto start = std::chrono::steady_clock::now();
        MapLoadResult loaded = LoadMapDirectory(directory);
        if (warnings) {
            warnings->insert(warnings->end(), loaded.warnings.begin(), loaded.warnings.end());
        }
        if (!loaded.ok()) {
            errors.insert(errors.end(), loaded.errors.begin(), loaded.errors.end());
            return nullptr;
        }
        auto world = Build(std::move(loaded.data), errors);
        if (world) {
            world->stats_.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (warnings) {
                warnings->insert(warnings->end(), world->buildWarnings_.begin(), world->buildWarnings_.end());
            }
        }
        return world;
    }

    std::unique_ptr<MapWorld> MapWorld::Build(MapData data, std::vector<std::string>& errors)
    {
        std::unique_ptr<MapWorld> world(new MapWorld());
        world->data_ = std::move(data);
        using clock = std::chrono::steady_clock;
        auto t0 = clock::now();
        world->terrain_.Build(world->data_.terrain);
        const TerrainField& terrain = world->terrain_;
        auto t1 = clock::now();
        if (!world->roads_.Build(world->data_, [&terrain](const float x, const float z) { return terrain.RawHeight(x, z); }, errors)) {
            return nullptr;
        }
        auto t2 = clock::now();
        world->terrain_.ConformToRoads(world->roads_);
        auto t3 = clock::now();
        world->lanes_.Build(world->roads_);
        auto t4 = clock::now();
        world->ground_ = std::make_unique<MapGround>(world->roads_, world->terrain_);
        world->objects_.Build(*world, world->buildWarnings_);
        auto t5 = clock::now();
        world->stats_.objectSeconds = std::chrono::duration<double>(t5 - t4).count();
        world->stats_.terrainSeconds = std::chrono::duration<double>(t1 - t0).count() + std::chrono::duration<double>(t3 - t2).count();
        world->stats_.roadSeconds = std::chrono::duration<double>(t2 - t1).count();
        world->stats_.laneSeconds = std::chrono::duration<double>(t4 - t3).count();
        return world;
    }

    SpawnSpec MapWorld::PlayerSpawn(const std::string& name) const
    {
        for (const auto& s : data_.traffic.playerSpawns) {
            if (name.empty() || s.name == name) {
                return s;
            }
        }
        if (!data_.traffic.playerSpawns.empty()) {
            return data_.traffic.playerSpawns.front();
        }
        SpawnSpec fallback;
        fallback.name = "lane";
        if (!lanes_.Lanes().empty()) {
            const Lane& lane = lanes_.Lanes().front();
            const LanePoint p = lane.Evaluate(std::min(lane.length, 10.0f));
            fallback.position = Vector2(p.position.X, p.position.Z);
            fallback.headingDeg = HeadingFromDirection(p.tangent.X, p.tangent.Z) * 180.0f / std::numbers::pi_v<float>;
        }
        return fallback;
    }

    Vector3 MapWorld::SpawnPosition(const SpawnSpec& spawn) const
    {
        return Vector3(spawn.position.X, ground_->HeightAt(spawn.position.X, spawn.position.Y), spawn.position.Y);
    }
}
