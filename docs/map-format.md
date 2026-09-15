# Map format decision

Status: decided 2026-09-14 (plan task MAP-001). Implemented by `simulator/src/Map` (schema v1 below).

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

## Schema v1 reference

All files are UTF-8 JSON objects with an integer `schemaVersion` (currently `1`). Unknown keys
are ignored; the loader reports every structural problem it finds in one pass (dotted paths).
Lengths are metres, angles degrees, positions `[x, z]` in the map plane (x east, z south,
north = -z). Headings: 0 = north, 90 = east (clockwise).

### map.json
| key | type | notes |
| --- | --- | --- |
| `id`, `displayName`, `description`, `author`, `license` | string | `id` is required |
| `files` | object | optional renames of `terrain`, `roads`, `objects`, `traffic` |

### terrain.json
| key | type | notes |
| --- | --- | --- |
| `size` | `[x, z]` | extent, centred on the origin (>= 200 m) |
| `cellSize` | number | height-field spacing, 1..10 m |
| `baseHeight` | number | |
| `noise` | object | `amplitude`, `wavelength`, `octaves`, `seed` (signed fBm) |
| `roadBlendWidth` | number | distance over which terrain blends into the road edge |
| `features[]` | object | `type` `hill`/`ridge`/`plateau`, `center`, `end` (ridge), `radius`, `height` (negative = basin) |
| `regions[]` | object | `type` `meadow`/`field`/`forest`/`town`/`square`/`yard`/`orchard`, `polygon`, `crop`, `seed`; later regions win. A `square` region is paved with cobbles and a `yard` with concrete slabs (both drawn as a separate ground mesh cut around the roads, a four-corner outline filled exactly); they drive and sound like cobbles and concrete |

### roads.json
`nodes[]`: `id`, `position`, optional `elevation`, `urban` (built-up area: urban speed limit,
sidewalks), `name`, `mainRoads[]` (roads with priority through this node), `control[]`
(`{road, control}` with `priority|right_hand|yield|stop|signal`), `cornerRadius`, `signals`.

`signals` makes the node a signalised junction and every approach `signal`:

```json
"signals": {"enabled": true, "green": 22, "amber": 3, "allRed": 2, "offset": 0,
            "groups": [["main"], ["r_east", "r_church"]]}
```

The groups take their green in turn, each followed by `amber` seconds of amber and `allRed`
seconds with everything red; the last second before a group's green is red-and-amber together.
`offset` shifts this node's cycle, so neighbouring junctions can be coordinated. Roads the
groups do not name get a phase of their own; with `groups` left out, the node's `mainRoads` take
one phase and the rest the other. A mast with a three-lens head is generated on the right-hand
kerb of every signalised approach, facing the traffic coming towards the junction.

`roads[]`: `id`, `name`, `number`, `class` (`I|II|III|local|residential|forest|track`),
`nodes[]` (>= 2, consecutive nodes >= 4 m apart), `lanesPerDirection`, `laneWidth`,
`edgeStripWidth`, `shoulderWidth`, `surface` (`asphalt|concrete|cobbles|gravel|dirt`),
`speedLimitKmh`, `urbanSpeedLimitKmh`, `centreLine` (`none|solid|dashed`), `edgeLines`,
`sidewalk` (`width`, `left`, `right`, `kerbHeight`; applied on urban stretches only),
`cornerRadius` (fillet at interior non-junction nodes), `oneWay`.

Derived at load time (`RoadNetwork`): straight-and-arc centrelines through the nodes, heights
from the terrain (80 m low-pass, pinned to node heights, flattened across intersections),
intersection patches with setbacks and kerb fillets, road pieces between intersections,
approach controls. `LaneGraph` derives lanes per direction, connectors with turn types,
conflicts and yield lists (priority, right-hand rule, left turn yields to oncoming), routes.

### objects.json
`buildings[]` (`type` house/cottage/block/church/barn/shop/hall/chapel, `position`,
`rotationDeg` = facade heading, `width`, `depth`, `eavesHeight`, `roofPitchDeg`, `floors`,
`wallColor`, `roofColor`, `seed`), Cars are also parked automatically along urban local and residential streets (see
`ObjectPlacement::PlaceStreetParking`); `vehicles[]` is for the ones the map author places.
`props[]` (`type` incl. `memorial`, `fuel_canopy`, `fuel_pump`, `position`, `rotationDeg`, `length`,
`scale`), `signs[]` (`code` from the Czech catalogue subset, `position`, `headingDeg` = the
direction the face points, `text`, `value`), `vehicles[]` (parked cars: `body`
hatchback/sedan/estate/suv/van, `position`, `rotationDeg` of the nose, `seed` for the style
variant, paint and plate), `trees[]` (`species`, `position`, `scale`, `seed`),
`forests[]` (`polygon`, `density` trees/m^2, `species[]` `{species, weight}`, `margin`, `seed`),
`avenues[]` (`road`, `fromNode`, `toNode`, `species`, `spacing`, `offset`, `left`, `right`, `seed`).

### traffic.json
`playerSpawns[]` (`name`, `position`, `headingDeg`), `densityPerKm`, `maxVehicles`,
`vehicles[]` (vehicle definition ids), `spawnMinDistance`, `despawnDistance`.

## Tools

- `carsim-mapvalidate --content content --map lipova` loads and builds a map, prints
  intersections with their controls and setbacks, lane counts, dead ends, grades and build
  times, and fails on structural problems. It runs as the `map_validate_lipova` CTest.
- `tools/maps/generate_lipova.py` is the authoring script of the sample map; the generated
  JSON is the source the simulator loads.
