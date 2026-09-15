#!/usr/bin/env python3
"""Grows the sample map "Lipová" into a wider region with more settlements.

The map under content/maps/lipova was authored first by tools/maps/generate_lipova.py and then
extended by hand (the town square, the chapel, the filling station, parked cars). This script is
additive and idempotent: it reads the JSON that is there, removes only what it produced on an
earlier run, and writes the enlarged world back. Run it from the repository root:

    python3 tools/maps/add_settlements.py

What it adds, on top of the original town:

  * a larger terrain (6400 x 7600 m at a 5 m grid) with hills around the new settlements,
  * four new places reached by three new class III roads:
      Březí        a village on the main road east of Lipová (E3),
      Podhájí      a village on a new southern ring from S2 round to E3,
      Nové Město   a small town in the north-east with its own square,
      Kamenice     a hamlet at the end of the south-western road,
  * the houses, signs, props, tree avenues and boundary signs that go with them,
  * player spawns at each new settlement.

Everything it writes carries either a new id or a "generated-by" marker, which is how a re-run
knows what to replace. Coordinates: map plane x (east) / z (south), metres; north is -z.
"""
import json
import math
import pathlib
import random

OUT = pathlib.Path(__file__).resolve().parents[2] / "content" / "maps" / "lipova"

# ----------------------------------------------------------------------------- the wider world
TERRAIN_SIZE = [6400, 7600]
# A 5 m grid over the bigger map keeps the terrain mesh within about a quarter more vertices than
# the 4 m grid did over the small one; the roads carry their own geometry, so the coarser ground
# is only visible in the open country.
TERRAIN_CELL = 5

# Existing nodes the new roads hang off. They become junctions, so they need a priority road.
JUNCTION_MAIN_ROADS = {"E3": ["main"], "F1": ["north"], "S2": ["south"], "SW1": ["south"], "E5": ["main"]}

NEW_NODES = {
    # Březí: a village straddling the main road east of Lipová, with one side street.
    "BR1": (1150, -170), "BR2": (980, -55),
    # Podhájí: a village on the new southern ring.
    "P1": (900, 900), "P2": (1500, 1020), "P3": (2200, 720), "PH1": (1210, 1270),
    # Nové Město: a small town in the north-east, on a loop off the forest road.
    "M1": (1700, -2280), "M2": (2150, -2520), "M3": (2620, -2150), "M4": (2500, -1700), "NM1": (2440, -2430),
    # Kamenice: a hamlet at the end of the south-western road.
    "K1": (-1900, 1450), "K2": (-2500, 1700),
}
NEW_URBAN = {"BR1", "BR2", "P1", "P2", "PH1", "M1", "M2", "M3", "NM1", "K1", "K2"}
NEW_NAMES = {"M2": "Nové Město", "P2": "Podhájí"}
NEW_MAIN_ROADS = {"P2": ["east_ring"], "M2": ["northeast"], "M3": ["northeast"]}

NEW_ROADS = [
    {"id": "r_brezi", "name": "Na Vyhlídce", "class": "residential", "nodes": ["E3", "BR1", "BR2"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "east_ring", "name": "Podhájská", "number": "III/15615", "class": "III",
     "nodes": ["S2", "P1", "P2", "P3", "E3"],
     "laneWidth": 2.75, "edgeStripWidth": 0.25, "shoulderWidth": 0.5, "speedLimitKmh": 90,
     "centreLine": "dashed", "edgeLines": True, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 70},
    {"id": "r_podhaji", "name": "Ke Hřišti", "class": "residential", "nodes": ["P1", "PH1", "P2"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "northeast", "name": "Novoměstská", "number": "III/15616", "class": "III",
     "nodes": ["F1", "M1", "M2", "M3", "M4", "E5"],
     "laneWidth": 2.75, "edgeStripWidth": 0.25, "shoulderWidth": 0.5, "speedLimitKmh": 90,
     "centreLine": "dashed", "edgeLines": True, "sidewalk": {"width": 1.8, "left": True, "right": True},
     "cornerRadius": 70},
    {"id": "r_mesto", "name": "Zámecká", "class": "residential", "nodes": ["M2", "NM1", "M3"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "southwest", "name": "Kamenická", "number": "III/15617", "class": "III",
     "nodes": ["SW1", "K1", "K2"],
     "laneWidth": 2.5, "edgeStripWidth": 0.0, "shoulderWidth": 0.5, "speedLimitKmh": 70,
     "centreLine": "none", "edgeLines": False, "cornerRadius": 45},
]

# Every object this script writes carries this marker, which is how a re-run knows what to
# replace. The map loader reads only the keys it knows, so the extra field is inert.
MARK = "generated-by"
MARK_VALUE = "add_settlements"

NEW_TERRAIN_FEATURES = [
    {"type": "hill", "center": [1500, 900], "radius": 700, "height": 26},       # the ridge Podhájí sits under
    {"type": "hill", "center": [2400, -2300], "radius": 820, "height": 40},     # the hill Nové Město climbs
    {"type": "ridge", "center": [2700, -1500], "end": [2950, -600], "radius": 400, "height": 26},
    {"type": "hill", "center": [-2300, 1600], "radius": 700, "height": 34},     # Kamenice in the hills
    {"type": "hill", "center": [-2900, -1400], "radius": 900, "height": 30},    # far north-west uplands
    {"type": "hill", "center": [2600, 1900], "radius": 900, "height": 18},      # far south-east
    {"type": "plateau", "center": [2150, -2480], "radius": 260, "height": 4},   # the town's own shelf
]

NEW_REGIONS = [
    {"type": "town", "polygon": [[820, -420], [1560, -420], [1560, 120], [820, 120]]},
    {"type": "town", "polygon": [[700, 620], [2350, 620], [2350, 1440], [700, 1440]]},
    {"type": "town", "polygon": [[1520, -2740], [2880, -2740], [2880, -1580], [1520, -1580]]},
    {"type": "town", "polygon": [[-2760, 1180], [-1640, 1180], [-1640, 1920], [-2760, 1920]]},
    {"type": "forest", "polygon": [[1700, -1500], [2600, -1450], [3050, -1900], [2950, -2750],
                                   [2350, -2900], [1600, -2800], [1450, -2100]],
     "density": 0.02, "margin": 8,
     "species": [{"species": "spruce", "weight": 0.55}, {"species": "pine", "weight": 0.25},
                 {"species": "beech", "weight": 0.2}], "seed": 21},
    {"type": "forest", "polygon": [[-3050, 400], [-2400, 700], [-2100, 1500], [-2600, 2100],
                                   [-3100, 1900], [-3150, 1000]],
     "density": 0.018, "margin": 8,
     "species": [{"species": "spruce", "weight": 0.4}, {"species": "beech", "weight": 0.4},
                 {"species": "oak", "weight": 0.2}], "seed": 22},
    {"type": "field", "polygon": [[600, 1500], [2400, 1500], [2400, 2600], [600, 2600]], "crop": "wheat", "seed": 31},
    {"type": "field", "polygon": [[-1600, 2000], [200, 2000], [200, 3100], [-1600, 3100]], "crop": "rape", "seed": 32},
    {"type": "field", "polygon": [[2300, 200], [3200, 200], [3200, 1400], [2300, 1400]], "crop": "maize", "seed": 33},
    {"type": "meadow", "polygon": [[-3200, -800], [-2000, -800], [-2000, 600], [-3200, 600]]},
    {"type": "meadow", "polygon": [[1400, -1450], [3000, -1450], [3000, -400], [1400, -400]]},
]

NEW_SPAWNS = [
    {"name": "brezi", "node_a": "E2", "node_b": "E3", "t": 0.80},
    {"name": "podhaji", "node_a": "P1", "node_b": "P2", "t": 0.25},
    {"name": "mesto", "node_a": "M1", "node_b": "M2", "t": 0.60},
    {"name": "kamenice", "node_a": "SW1", "node_b": "K1", "t": 0.70},
]

ALL_NODES = {}   # filled from the map once it is loaded, so seg() can use existing nodes too


# ----------------------------------------------------------------------------- geometry helpers
def heading_deg(dx, dz):
    return math.degrees(math.atan2(dx, -dz)) % 360.0


def direction(h_deg):
    h = math.radians(h_deg)
    return math.sin(h), -math.cos(h)


def right_of(h_deg):
    return direction(h_deg + 90.0)


def along(a, b, t):
    return a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t


def offset(p, h_deg, right, forward=0.0):
    rx, rz = right_of(h_deg)
    fx, fz = direction(h_deg)
    return round(p[0] + rx * right + fx * forward, 1), round(p[1] + rz * right + fz * forward, 1)


def seg(a, b):
    pa, pb = ALL_NODES[a], ALL_NODES[b]
    return pa, pb, heading_deg(pb[0] - pa[0], pb[1] - pa[1]), math.hypot(pb[0] - pa[0], pb[1] - pa[1])


def mine(entry):
    return entry.get(MARK) == MARK_VALUE


# ----------------------------------------------------------------------------- content
def settlement_content():
    """Buildings, signs and props for the four new places."""
    rng = random.Random(1907)
    buildings, signs, props, avenues = [], [], [], []

    def house(pos, heading, kind="house", width=None, depth=None, eaves=None, floors=None, pitch=None, seed=None):
        buildings.append({
            MARK: MARK_VALUE,
            "type": kind,
            "position": [round(pos[0], 1), round(pos[1], 1)],
            "rotationDeg": round(heading % 360.0, 1),
            "width": width or round(rng.uniform(10.5, 14.0), 1),
            "depth": depth or round(rng.uniform(8.5, 11.0), 1),
            "eavesHeight": eaves or (6.4 if kind == "house" else 3.6),
            "floors": floors or (2 if kind == "house" else 1),
            "roofPitchDeg": pitch or rng.choice([35, 38, 42, 45]),
            "seed": seed if seed is not None else rng.randint(1, 10_000),
        })

    def row(a, b, setback, side, spacing, kinds, start=0.06, end=0.94, skip=()):
        pa, pb, h, length = seg(a, b)
        n = max(1, int(length * (end - start) / spacing))
        for i in range(n):
            t = start + (end - start) * (i + 0.5) / n
            if any(lo <= t <= hi for lo, hi in skip):
                continue
            p = offset(along(pa, pb, t), h, side * setback)
            face = (h + 90.0) if side < 0 else (h - 90.0)
            house(p, face, rng.choice(kinds))

    def sign(code, node_a, node_b, t, side, text="", value=0.0, right=6.0):
        pa, pb, h, _ = seg(node_a, node_b)
        p = offset(along(pa, pb, t), h, side * right)
        s = {MARK: MARK_VALUE, "code": code, "position": list(p), "headingDeg": round((h + 180.0) % 360.0, 1)}
        if text:
            s["text"] = text
        if value:
            s["value"] = value
        signs.append(s)

    def prop(kind, node_a, node_b, t, side, right=7.5, length=0.0, scale=1.0, face_road=True):
        pa, pb, h, _ = seg(node_a, node_b)
        p = offset(along(pa, pb, t), h, side * right)
        rot = (h - 90.0) % 360.0 if side > 0 else (h + 90.0) % 360.0
        d = {MARK: MARK_VALUE, "type": kind, "position": list(p), "rotationDeg": round(rot if face_road else h, 1)}
        if length:
            d["length"] = length
        if scale != 1.0:
            d["scale"] = scale
        props.append(d)

    # --- Březí: a street village on the main road, one side street off it.
    row("E2", "E3", 16.0, -1, 24, ["cottage", "house"], start=0.62, end=0.97)
    row("E2", "E3", 16.0, +1, 24, ["house", "cottage"], start=0.62, end=0.97)
    row("E3", "BR1", 13.0, -1, 20, ["cottage", "house"], start=0.15, end=0.92)
    row("E3", "BR1", 13.0, +1, 20, ["house"], start=0.15, end=0.92)
    row("BR1", "BR2", 13.0, -1, 20, ["cottage"], start=0.10, end=0.90)
    house((1290, -300), 300, "chapel", width=5, depth=7, eaves=4.4, floors=1, pitch=50, seed=101)
    house((1060, -90), 140, "barn", width=20, depth=11, eaves=5.2, floors=1, pitch=40, seed=102)
    sign("IZ4a", "E2", "E3", 0.575, +1, text="Březí")
    sign("IZ4b", "E3", "E2", 0.425, +1, text="Březí")
    sign("P4", "BR1", "E3", 0.9, +1)
    for t in (0.70, 0.80, 0.90):
        prop("lamp", "E2", "E3", t, +1, right=6.4)
    prop("bus_stop", "E2", "E3", 0.86, +1)

    # --- Podhájí: a bigger village on the ring, with a green and a shop.
    row("P1", "P2", 16.0, -1, 22, ["house", "cottage"], start=0.12, end=0.92)
    row("P1", "P2", 16.0, +1, 22, ["house", "shop", "cottage"], start=0.12, end=0.92)
    row("P1", "PH1", 13.0, -1, 20, ["cottage", "house"], start=0.12, end=0.92)
    row("PH1", "P2", 13.0, +1, 20, ["house", "cottage"], start=0.10, end=0.90)
    house((1180, 960), 0, "church", width=12, depth=26, eaves=8.0, floors=1, pitch=50, seed=110)
    house((1255, 968), 0, "hall", width=18, depth=12, eaves=7.0, floors=2, pitch=38, seed=111)
    house((1000, 1180), 200, "barn", width=24, depth=12, eaves=5.4, floors=1, pitch=38, seed=112)
    house((1420, 1180), 160, "shed", width=8, depth=6, eaves=2.8, floors=1, pitch=30, seed=113)
    sign("IZ4a", "P1", "P2", 0.08, +1, text="Podhájí")
    sign("IZ4b", "P2", "P1", 0.08, +1, text="Podhájí")
    sign("IZ4a", "P3", "P2", 0.10, +1, text="Podhájí")
    sign("IZ4b", "P2", "P3", 0.10, +1, text="Podhájí")
    sign("P4", "PH1", "P2", 0.90, +1)
    sign("P4", "S2", "P1", 0.03, +1)
    sign("IS3c", "S2", "P1", 0.10, +1, text="Podhájí 9 / Nové Město 31")
    sign("B20a", "P1", "P2", 0.10, +1, value=50)
    for t in (0.2, 0.4, 0.6, 0.8):
        prop("lamp", "P1", "P2", t, +1, right=6.4)
    prop("bus_stop", "P1", "P2", 0.46, +1)
    prop("bench", "P1", "P2", 0.50, -1, right=8.5)
    prop("fence", "P1", "PH1", 0.30, -1, right=8.0, length=70)
    avenues.append({MARK: MARK_VALUE, "road": "east_ring", "fromNode": "P3", "toNode": "E3", "species": "linden",
                    "spacing": 22, "offset": 3.2, "left": True, "right": True, "seed": 11})

    # --- Nové Město: a small town with a square, a couple of blocks and a filling of shops.
    row("M1", "M2", 16.5, -1, 22, ["house", "cottage"], start=0.55, end=0.95)
    row("M1", "M2", 16.5, +1, 22, ["house"], start=0.55, end=0.95)
    row("M2", "M3", 16.5, -1, 21, ["house", "shop"], start=0.06, end=0.55)
    row("M2", "M3", 16.5, +1, 21, ["house"], start=0.06, end=0.55)
    row("M2", "NM1", 13.0, -1, 19, ["house", "cottage"], start=0.15, end=0.92)
    row("M2", "NM1", 13.0, +1, 19, ["house"], start=0.15, end=0.92)
    row("NM1", "M3", 13.0, +1, 19, ["cottage", "house"], start=0.10, end=0.90)
    # The square: a church, a town hall and a terrace of shops around an open space.
    house((2110, -2600), 20, "church", width=13, depth=28, eaves=9.0, floors=1, pitch=52, seed=120)
    house((2215, -2585), 20, "hall", width=24, depth=13, eaves=9.0, floors=3, pitch=40, seed=121)
    house((2095, -2455), 200, "shop", width=17, depth=12, eaves=7.0, floors=2, pitch=36, seed=122)
    house((2175, -2440), 200, "shop", width=15, depth=12, eaves=7.0, floors=2, pitch=36, seed=123)
    house((2250, -2430), 200, "house", width=13, depth=11, eaves=6.8, floors=2, pitch=40, seed=124)
    for i, pos in enumerate([(2330, -2620), (2330, -2560), (2330, -2500)]):
        house(pos, 90, "block", width=34, depth=12, eaves=17.5, floors=6, pitch=8, seed=130 + i)
    house((1900, -2340), 250, "barn", width=22, depth=11, eaves=5.2, floors=1, pitch=40, seed=140)
    sign("IZ4a", "M1", "M2", 0.50, +1, text="Nové Město")
    sign("IZ4b", "M2", "M1", 0.50, +1, text="Nové Město")
    sign("IZ4a", "M4", "M3", 0.55, +1, text="Nové Město")
    sign("IZ4b", "M3", "M4", 0.45, +1, text="Nové Město")
    sign("P2", "M1", "M2", 0.92, +1)
    sign("P4", "NM1", "M2", 0.90, +1)
    sign("P4", "NM1", "M3", 0.90, +1)
    sign("IS3c", "F1", "M1", 0.12, +1, text="Nové Město 8 / Lipová 24")
    sign("B20a", "M1", "M2", 0.55, +1, value=50)
    sign("A14", "F1", "M1", 0.40, +1)
    for t in (0.62, 0.72, 0.82, 0.92):
        prop("lamp", "M1", "M2", t, +1, right=6.6)
    for t in (0.1, 0.2, 0.3, 0.4):
        prop("lamp", "M2", "M3", t, -1, right=6.6)
    prop("bus_stop", "M1", "M2", 0.88, +1)
    prop("bench", "M2", "M3", 0.08, -1, right=9.0)
    prop("bin", "M2", "M3", 0.10, -1, right=9.0)
    avenues.append({MARK: MARK_VALUE, "road": "northeast", "fromNode": "M4", "toNode": "E5", "species": "birch",
                    "spacing": 24, "offset": 3.4, "left": True, "right": True, "seed": 12})

    # --- Kamenice: a hamlet of cottages and farm buildings at the end of the road.
    row("K1", "K2", 15.0, -1, 26, ["cottage"], start=0.25, end=0.95)
    row("K1", "K2", 15.0, +1, 26, ["cottage", "house"], start=0.25, end=0.95)
    house((-2520, 1630), 120, "barn", width=22, depth=11, eaves=5.2, floors=1, pitch=42, seed=150)
    house((-2450, 1770), 300, "cottage", width=10, depth=8, eaves=3.4, floors=1, pitch=46, seed=151)
    house((-1960, 1420), 60, "chapel", width=4.5, depth=6.5, eaves=4.2, floors=1, pitch=50, seed=152)
    sign("IZ4a", "K1", "K2", 0.18, +1, text="Kamenice")
    sign("IZ4b", "K2", "K1", 0.82, +1, text="Kamenice")
    sign("A7a", "SW1", "K1", 0.35, +1)
    sign("A14", "SW1", "K1", 0.15, +1)
    prop("bus_stop", "K1", "K2", 0.35, +1)
    prop("timber_stack", "K1", "K2", 0.55, -1, right=9.0, length=10)
    prop("fence", "K1", "K2", 0.30, +1, right=9.0, length=50)
    return buildings, signs, props, avenues


# ----------------------------------------------------------------------------- rewrite
def load(name):
    return json.loads((OUT / name).read_text(encoding="utf-8"))


def dump(name, data):
    path = OUT / name
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {path.relative_to(pathlib.Path.cwd())} ({path.stat().st_size} bytes)")


def main():
    roads = load("roads.json")
    terrain = load("terrain.json")
    objects = load("objects.json")
    traffic = load("traffic.json")

    new_road_ids = {r["id"] for r in NEW_ROADS}
    # Drop what an earlier run of this script added.
    roads["nodes"] = [n for n in roads["nodes"] if n["id"] not in NEW_NODES]
    roads["roads"] = [r for r in roads["roads"] if r["id"] not in new_road_ids]

    for nid, (x, z) in NEW_NODES.items():
        node = {"id": nid, "position": [x, z]}
        if nid in NEW_URBAN:
            node["urban"] = True
        if nid in NEW_NAMES:
            node["name"] = NEW_NAMES[nid]
        if nid in NEW_MAIN_ROADS:
            node["mainRoads"] = NEW_MAIN_ROADS[nid]
        roads["nodes"].append(node)
    for nid, mains in JUNCTION_MAIN_ROADS.items():
        for node in roads["nodes"]:
            if node["id"] == nid:
                node["mainRoads"] = mains
                node["urban"] = node.get("urban", nid in ("E3",))
    roads["roads"].extend(NEW_ROADS)

    ALL_NODES.clear()
    for node in roads["nodes"]:
        ALL_NODES[node["id"]] = (node["position"][0], node["position"][1])

    terrain["size"] = TERRAIN_SIZE
    terrain["cellSize"] = TERRAIN_CELL
    known_features = {json.dumps(f, sort_keys=True) for f in NEW_TERRAIN_FEATURES}
    terrain["features"] = [f for f in terrain["features"] if json.dumps(f, sort_keys=True) not in known_features]
    terrain["features"].extend(NEW_TERRAIN_FEATURES)
    known_regions = {json.dumps(r, sort_keys=True) for r in NEW_REGIONS}
    kept = [r for r in terrain.get("regions", []) if json.dumps(r, sort_keys=True) not in known_regions]
    # Region order matters (later wins), and the town/square/yard regions of Lipová must stay on
    # top, so the new outer regions go in front of everything the original map declared.
    terrain["regions"] = NEW_REGIONS + kept

    buildings, signs, props, avenues = settlement_content()
    objects["buildings"] = [b for b in objects["buildings"] if not mine(b)] + buildings
    objects["signs"] = [s for s in objects["signs"] if not mine(s)] + signs
    objects["props"] = [p for p in objects["props"] if not mine(p)] + props
    objects["avenues"] = [a for a in objects.get("avenues", []) if not mine(a)] + avenues

    spawn_names = {s["name"] for s in NEW_SPAWNS}
    traffic["playerSpawns"] = [s for s in traffic["playerSpawns"] if s["name"] not in spawn_names]
    for spec in NEW_SPAWNS:
        pa, pb, h, _ = seg(spec["node_a"], spec["node_b"])
        p = offset(along(pa, pb, spec["t"]), h, 1.7)   # right-hand lane, heading towards node_b
        traffic["playerSpawns"].append({"name": spec["name"], "position": list(p), "headingDeg": round(h, 1)})

    dump("roads.json", roads)
    dump("terrain.json", terrain)
    dump("objects.json", objects)
    dump("traffic.json", traffic)
    print(f"settlements: {len(buildings)} buildings, {len(signs)} signs, {len(props)} props, "
          f"{len(avenues)} avenues, {len(NEW_SPAWNS)} spawns")


if __name__ == "__main__":
    main()
