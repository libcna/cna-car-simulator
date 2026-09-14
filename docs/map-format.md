# Map format decision

Status: decided 2026-09-14 (plan task MAP-001). Implemented by `simulator/src/Map`.

## Requirements

A map must describe metadata and schema version, terrain and material regions, roads
(segments, lanes, widths, surfaces, sidewalks, curbs, markings), intersections and lane
connectivity, signs and signals, buildings, props, vegetation, forests, fields, collision
geometry, spawn points, traffic routes/destinations, speed limits, road classifications and
optional audio zones. Visual data and simulation/navigation data must stay conceptually
separate, malformed maps must be detectable, and the format must evolve with explicit versions.

## Candidates compared

| Criterion | SQLite | JSON + external binaries | Custom binary | Hybrid (JSON source, generated runtime data) |
| --- | --- | --- | --- | --- |
| Versionability in git | poor (binary diffs) | excellent (text diffs) | poor | excellent for the source |
| Hand editing / authoring | needs tooling | any editor | needs tooling | any editor |
| Validation | SQL constraints + tool | schema validator tool | tool | schema validator tool |
| Loading speed | good (indexed) | fine for maps of a few MB; parsing is milliseconds | best | fine; cache possible |
| Future world size | good | needs chunking | good | chunked source files + generated cache |
| Schema evolution | migrations | `schemaVersion` + upgrader | versioned records | `schemaVersion` + upgrader |
| Debugging | needs sqlite3 CLI | readable | hex dumps | readable |
| Tooling cost | vendor SQLite (not in the approved stack; Sharp Runtime has no SQLite) | Sharp Runtime `System::Text::Json` already available | writer + reader | as JSON |
| Deterministic loading | yes | yes | yes | yes |
| Asset references | rows | paths | paths | paths |

## Decision

**Hybrid: authored maps are JSON text files (schema-versioned) referencing external assets;
runtime geometry (road meshes, terrain meshes, lightmaps, collision shapes, lane graph) is
generated deterministically from that source at load time, with an optional project-owned
binary cache (`.cmapcache`) to be added only if load time exceeds the budget (2 s for the sample
map).**

Reasons: text maps diff cleanly in git and are reviewable; validation is one tool over one
schema; Sharp Runtime already provides a JSON parser so no new dependency enters the build;
the sample map is small (hundreds of KB); and generating render geometry from the road/lane
description keeps simulation data authoritative and the render geometry derived, which is what
the traffic AI and collision system need. SQLite would add a vendored dependency for benefits
(indexed partial loading) the project does not need before maps grow to many square kilometres;
the hybrid path leaves that door open (chunked source files, a cache), which is recorded as a
deferred feature.

## Layout

```
content/maps/<map-name>/
  map.json            metadata, schemaVersion, references to the files below
  terrain.json        heightfield parameters + heights file reference (PNG16 or raw)
  roads.json          roads, intersections, speed limits, classifications
  objects.json        buildings, props, signs, street lights, vegetation, forests, fields
  traffic.json        player spawn points, traffic spawn points, routes, destinations, densities
  audio.json          optional ambient zones
```

Every file carries `"schemaVersion"`. The loader refuses unknown major versions and upgrades
older known versions in memory. `tools/map-validate` (also a CTest) checks referential integrity
(road ids, intersection endpoints, lane connectivity, asset paths), geometric sanity (lane
widths, curvature vs speed, overlaps) and reports warnings for questionable data.

## Separation of concerns

- **Simulation data**: `RoadNetwork` (roads, centreline splines, widths, surfaces), `LaneGraph`
  (lanes with direction, width, neighbours, links, intersection connectors, speed limit, priority),
  `TerrainField` (height/normal queries), `CollisionWorld` inputs, spawn points, routes.
- **Visual data**: generated `RoadGeometry` (surface, markings, curbs, sidewalks), terrain
  mesh with material blending, building/prop/vegetation instances, sign meshes, lightmaps.

The visual generators consume simulation data, never the other way round.
