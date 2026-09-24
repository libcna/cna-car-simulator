#!/usr/bin/env python3
"""Adds Hrad Lipník, a medieval castle on a wooded hill west of Lipová, and the forest track to it.

Stage 3 of the Lipová map pipeline. Like stage 2 it is additive and idempotent: it reads the map
that is there, removes only what an earlier run of itself wrote, and writes it back. Run the
pipeline through its one entry point:

    python3 tools/maps/build_map.py

What it adds:

  * a steep hill, about 72 m over the meadows, with the castle on its top,
  * spruce, beech and oak forest over its slopes and a clearing round the castle on the top, so
    that the castle stands out over the trees,
  * a gravel forest track from the main road at W3, climbing the hill in two hairpins and
    once round the castle below its walls, through the gatehouse and into the courtyard,
  * the castle: a curtain wall with battlements, four round corner towers, a gatehouse over the
    track, a tall keep and the palace,
  * a direction sign at the junction, a stop sign where the track meets the road, and a player
    spawn below the gate.

Everything it writes carries a new id or the "generated-by": "add_castle" marker.
Coordinates: map plane x (east) / z (south), metres; north is -z.
"""
import argparse
import json
import math
import pathlib

DEFAULT_OUT = pathlib.Path(__file__).resolve().parents[2] / "content" / "maps" / "lipova"

MARK = "generated-by"
MARK_VALUE = "add_castle"

CASTLE_NAME = "Hrad Lipník"
CASTLE = (-2150.0, -250.0)          # centre of the courtyard, on the hill top
HILL = {"type": "hill", "center": [-2150, -250], "radius": 380, "height": 72}

# The track: from W3 south-west across the meadow into the forest, up the southern slope in two
# long hairpins, then out of the trees and once round the castle below its walls -- the
# attacker's unshielded right side towards them -- to the gate, which faces the last node.
TRACK_NODES = {
    "HT1": (-1720, 160),
    "HT2": (-1770, -250),
    "HT3": (-1964, -472),
    "HT4": (-2169, -469),
    "HT5": (-2007, -332),
    "HT6": (-2033, -207),
    "HT7": (-2131, -140),
    "HT8": (-2227, -186),
    "HT9": (-2235, -273),
    "HT10": (-2182, -319),
    "HT11": (-2128, -310),
}
TRACK_ROAD_ID = "castle_track"


def mine(entry):
    return entry.get(MARK) == MARK_VALUE


def heading_deg(dx, dz):
    return math.degrees(math.atan2(dx, -dz)) % 360.0


def direction(h_deg):
    h = math.radians(h_deg)
    return math.sin(h), -math.cos(h)


def castle_frame():
    """Unit vectors of the castle: g towards the gate (down the approach), r to its right."""
    gx, gz = TRACK_NODES["HT11"][0] - CASTLE[0], TRACK_NODES["HT11"][1] - CASTLE[1]
    n = math.hypot(gx, gz)
    g = (gx / n, gz / n)
    front = heading_deg(g[0], g[1])
    r = direction(front + 90.0)
    return g, r, front


def local(u, v):
    """Castle-local (u right, v towards the gate) to map coordinates."""
    g, r, _ = castle_frame()
    return round(CASTLE[0] + r[0] * u + g[0] * v, 1), round(CASTLE[1] + r[1] * u + g[1] * v, 1)


def castle_buildings():
    _, _, front = castle_frame()
    out = []

    def piece(kind, u, v, heading, width, depth, eaves, pitch=0.0, floors=1, seed=1):
        out.append({
            MARK: MARK_VALUE,
            "type": kind,
            "position": list(local(u, v)),
            "rotationDeg": round(heading % 360.0, 1),
            "width": width,
            "depth": depth,
            "eavesHeight": eaves,
            "floors": floors,
            "roofPitchDeg": pitch,
            "seed": seed,
        })

    half_w, half_d = 28.0, 20.0          # the enclosure: 56 x 40 m between the tower centres
    gate_w = 11.0
    # Curtain walls, each facing outwards. The front is split by the gatehouse.
    seg = half_w - gate_w * 0.5
    piece("castle_wall", -(gate_w * 0.5 + seg * 0.5), half_d, front, round(seg, 1), 2.4, 9.0, seed=9101)
    piece("castle_wall", +(gate_w * 0.5 + seg * 0.5), half_d, front, round(seg, 1), 2.4, 9.0, seed=9102)
    piece("castle_wall", 0.0, -half_d, front + 180.0, 2 * half_w, 2.4, 9.5, seed=9103)
    piece("castle_wall", half_w, 0.0, front + 90.0, 2 * half_d, 2.4, 9.0, seed=9104)
    piece("castle_wall", -half_w, 0.0, front - 90.0, 2 * half_d, 2.4, 9.0, seed=9105)
    # Round corner towers with tall cones.
    for i, (u, v) in enumerate([(-half_w, half_d), (half_w, half_d), (half_w, -half_d), (-half_w, -half_d)]):
        piece("castle_tower", u, v, front, 8.0, 8.0, 15.0 if v > 0 else 17.0, pitch=62, seed=9110 + i)
    # The gatehouse over the track, the keep, and the palace against the back wall.
    piece("castle_gate", 0.0, half_d, front, gate_w, 7.0, 12.0, seed=9120)
    piece("castle_keep", -14.0, -8.0, front, 10.0, 10.0, 27.0, pitch=58, seed=9121)
    piece("castle_palace", 9.0, -13.3, front, 26.0, 10.0, 11.0, pitch=52, floors=3, seed=9122)
    return out


def build(out_dir, log=print):
    """Add the castle to the map in out_dir. Safe to run repeatedly."""
    out = pathlib.Path(out_dir)

    def load(name):
        return json.loads((out / name).read_text(encoding="utf-8"))

    def dump(name, data):
        path = out / name
        path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        if log:
            log(f"  castle: {name} ({path.stat().st_size} bytes)")

    roads = load("roads.json")
    terrain = load("terrain.json")
    objects = load("objects.json")
    traffic = load("traffic.json")
    card = load("map.json")

    g, r, front = castle_frame()
    gate_out = local(0.0, 28.0)
    courtyard = local(0.0, 4.0)
    nodes = dict(TRACK_NODES)
    nodes["HGO"] = gate_out
    nodes["HGI"] = courtyard

    # --- roads: drop an earlier run, then add the track and make W3 a junction.
    roads["nodes"] = [n for n in roads["nodes"] if n["id"] not in nodes]
    roads["roads"] = [x for x in roads["roads"] if x["id"] != TRACK_ROAD_ID]
    for nid, (x, z) in nodes.items():
        node = {"id": nid, "position": [x, z]}
        if nid == "HGI":
            node["name"] = CASTLE_NAME
        roads["nodes"].append(node)
    for node in roads["nodes"]:
        if node["id"] == "W3":
            node["mainRoads"] = ["main", "south"]
            node["control"] = [{"road": TRACK_ROAD_ID, "control": "stop"}]
    roads["roads"].append({
        MARK: MARK_VALUE, "id": TRACK_ROAD_ID, "name": "lesní cesta k hradu", "class": "track",
        "nodes": ["W3", *TRACK_NODES, "HGO", "HGI"],
        "laneWidth": 1.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.3, "surface": "gravel",
        "speedLimitKmh": 30, "centreLine": "none", "edgeLines": False, "cornerRadius": 14,
    })
    all_nodes = {n["id"]: tuple(n["position"]) for n in roads["nodes"]}

    # --- terrain: the hill; forest on the slopes, a clearing on the top.
    key = json.dumps(HILL, sort_keys=True)
    terrain["features"] = [f for f in terrain["features"] if json.dumps(f, sort_keys=True) != key] + [HILL]
    # The wood is a ring: the outline round the hill, and the clearing round the castle cut out of
    # it through a slit along the approach (a polygon with a bridged hole, which the even-odd
    # point test reads as a hole).
    start = math.atan2(g[1], g[0])
    outer = [(CASTLE[0] + 440 * math.cos(start + k * math.pi / 12), CASTLE[1] + 440 * math.sin(start + k * math.pi / 12)) for k in range(25)]
    inner = [(CASTLE[0] + 130 * math.cos(start - k * math.pi / 12), CASTLE[1] + 130 * math.sin(start - k * math.pi / 12)) for k in range(25)]
    ring = [[round(x, 1), round(z, 1)] for x, z in outer + inner]
    wood = {"polygon": ring, "density": 0.022, "margin": 8,
            "species": [{"species": "spruce", "weight": 0.5}, {"species": "beech", "weight": 0.35},
                        {"species": "oak", "weight": 0.15}], "seed": 41}
    forest = {MARK: MARK_VALUE, "type": "forest", "polygon": ring}
    clearing = {MARK: MARK_VALUE, "type": "meadow",
                "polygon": [list(local(-50, -40)), list(local(50, -40)), list(local(50, 45)), list(local(-50, 45))]}
    # Later regions win where they overlap: the clearing goes after the forest.
    terrain["regions"] = [x for x in terrain.get("regions", []) if not mine(x)] + [forest, clearing]

    # --- objects: the castle, and the signs for it.
    signs = []

    def sign(code, a, b, t, side, text="", right=5.0):
        pa, pb = all_nodes[a], all_nodes[b]
        h = heading_deg(pb[0] - pa[0], pb[1] - pa[1])
        p = (pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t)
        rx, rz = direction(h + 90.0)
        s = {MARK: MARK_VALUE, "code": code,
             "position": [round(p[0] + rx * side * right, 1), round(p[1] + rz * side * right, 1)],
             "headingDeg": round((h + 180.0) % 360.0, 1)}
        if text:
            s["text"] = text
        signs.append(s)

    sign("IS3c", "W2", "W3", 0.93, +1, text=f"{CASTLE_NAME} 1", right=6.5)
    sign("IS3c", "SW1", "W3", 0.93, +1, text=f"{CASTLE_NAME} 1", right=6.5)
    sign("P6", "HT1", "W3", 0.93, +1, right=3.5)
    sign("A7a", "W3", "HT1", 0.10, +1, right=3.5)
    objects["buildings"] = [b for b in objects["buildings"] if not mine(b)] + castle_buildings()
    objects["signs"] = [s for s in objects["signs"] if not mine(s)] + signs
    objects["forests"] = [f for f in objects.get("forests", []) if not mine(f)] + [dict(wood, **{MARK: MARK_VALUE})]

    # --- a spawn on the track below the gate, facing up to it.
    traffic["playerSpawns"] = [s for s in traffic["playerSpawns"] if s["name"] != "hrad"]
    pa, pb = all_nodes["HT11"], all_nodes["HGO"]
    h = heading_deg(pb[0] - pa[0], pb[1] - pa[1])
    rx, rz = direction(h + 90.0)
    p = (pa[0] + (pb[0] - pa[0]) * 0.25 + rx * 0.9, pa[1] + (pb[1] - pa[1]) * 0.25 + rz * 0.9)
    traffic["playerSpawns"].append({"name": "hrad", "position": [round(p[0], 1), round(p[1], 1)], "headingDeg": round(h, 1)})

    sentence = f" On a wooded hill west of Lipová stands the medieval castle {CASTLE_NAME}, reached by a forest track."
    card["description"] = card["description"].replace(sentence, "") + sentence

    dump("map.json", card)
    dump("roads.json", roads)
    dump("terrain.json", terrain)
    dump("objects.json", objects)
    dump("traffic.json", traffic)
    if log:
        log(f"  castle: {len(castle_buildings())} castle pieces, {len(signs)} signs, track of {len(nodes)} new nodes")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Stage 3 of the Lipová map pipeline.")
    ap.add_argument("--out", default=str(DEFAULT_OUT), help="map directory to add the castle to (default: the shipped map)")
    ap.add_argument("--stage-only", action="store_true", help="run this stage on its own, over the map that is already there")
    args = ap.parse_args(argv)
    if not args.stage_only:
        ap.error("this is stage 3 of the pipeline; run tools/maps/build_map.py, "
                 "or pass --stage-only to add the castle to the map that is already in place")
    build(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
