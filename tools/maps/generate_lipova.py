#!/usr/bin/env python3
"""Authoring script for the sample map "Lipová" (content/maps/lipova).

The JSON files under content/maps/lipova are the map source that the simulator loads. This
script generates them deterministically so that repetitive content (rows of houses, tree
avenues, sign placement) stays consistent and easy to revise. Run it from the repository root:

    python3 tools/maps/generate_lipova.py

Coordinates: map plane x (east) / z (south), metres; north is -z. Headings: 0 = north,
90 = east (clockwise).
"""
import json
import math
import pathlib
import random

OUT = pathlib.Path(__file__).resolve().parents[2] / "content" / "maps" / "lipova"

# ----------------------------------------------------------------------------- nodes
NODES = {
    # main road II/156, west -> east
    "W3": (-1500, 300), "W2": (-760, 90), "W1": (-330, 10), "SQ": (0, 0), "E1": (330, -25),
    "E2": (720, -95), "E3": (1300, -330), "E4": (1720, -820), "E5": (1820, -1400),
    # north road III/15612 through the forest, back to E5
    "N1": (10, -290), "N2": (-15, -610), "N3": (-40, -930), "F4": (-420, -1520), "F3": (-20, -2050),
    "F2": (620, -2150), "F1": (1320, -1920),
    # south road III/15613 through the fields, W3 -> SQ
    "SW1": (-1250, 950), "S3": (-520, 1150), "S2": (80, 560), "S1": (30, 270),
    # residential grid
    "RW1": (-340, -300), "RE1": (320, 280), "RN1": (-330, -620), "RC1": (300, -600),
    # village road (west loop)
    "V1": (-700, -1480), "V2": (-1050, -1200), "V3": (-1100, -700),
    # forest track (gravel, dead end with turning loop)
    "FT1": (-120, -2400), "FT2": (-60, -2650), "FT3": (-20, -2720), "FT4": (-100, -2740),
}
URBAN = {"W2", "W1", "SQ", "E1", "E2", "N1", "N2", "N3", "S2", "S1", "RW1", "RE1", "RN1", "RC1"}
NAMES = {"SQ": "náměstí", "E1": "U kaple", "N2": "sídliště"}
MAIN_ROADS = {
    "SQ": ["main"], "E1": ["main"], "W1": ["main"], "W2": ["main"], "N1": ["north"], "N2": ["north"],
    "S1": ["south"], "F4": ["north"], "F3": ["north"],
}
CONTROL = {"F3": [{"road": "forest_track", "control": "stop"}]}
# Signalised junctions: the main road takes one phase, the side streets the other.
SIGNALS = {
    "E1": {"enabled": True, "green": 22, "amber": 3, "allRed": 2,
           "groups": [["main"], ["r_east", "r_church"]]},
}

ROADS = [
    {"id": "main", "name": "Hlavní", "number": "II/156", "class": "II",
     "nodes": ["W3", "W2", "W1", "SQ", "E1", "E2", "E3", "E4", "E5"],
     "laneWidth": 3.0, "edgeStripWidth": 0.25, "shoulderWidth": 0.5, "speedLimitKmh": 90,
     "centreLine": "dashed", "edgeLines": True, "sidewalk": {"width": 1.8, "left": True, "right": True},
     "cornerRadius": 90},
    {"id": "north", "name": "Lesní", "number": "III/15612", "class": "III",
     "nodes": ["SQ", "N1", "N2", "N3", "F4", "F3", "F2", "F1", "E5"],
     "laneWidth": 2.75, "edgeStripWidth": 0.25, "shoulderWidth": 0.5, "speedLimitKmh": 90,
     "centreLine": "dashed", "edgeLines": True, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 60},
    {"id": "south", "name": "Polní", "number": "III/15613", "class": "III",
     "nodes": ["W3", "SW1", "S3", "S2", "S1", "SQ"],
     "laneWidth": 2.75, "edgeStripWidth": 0.25, "shoulderWidth": 0.5, "speedLimitKmh": 90,
     "centreLine": "dashed", "edgeLines": True, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 60},
    {"id": "r_west", "name": "Krátká", "class": "residential", "nodes": ["W1", "RW1", "N1"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "r_east", "name": "Zahradní", "class": "residential", "nodes": ["E1", "RE1", "S1"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "r_estate", "name": "Sídlištní", "class": "residential", "nodes": ["N2", "RN1", "RW1"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "r_church", "name": "Kostelní", "class": "residential", "nodes": ["N2", "RC1", "E1"],
     "laneWidth": 2.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.0, "speedLimitKmh": 50,
     "centreLine": "none", "edgeLines": False, "sidewalk": {"width": 1.6, "left": True, "right": True},
     "cornerRadius": 12},
    {"id": "village", "name": "K Boru", "class": "III", "nodes": ["F4", "V1", "V2", "V3", "W2"],
     "laneWidth": 2.5, "edgeStripWidth": 0.0, "shoulderWidth": 0.5, "speedLimitKmh": 70,
     "centreLine": "none", "edgeLines": False, "cornerRadius": 45},
    {"id": "forest_track", "name": "lesní cesta", "class": "track", "nodes": ["F3", "FT1", "FT2"],
     "laneWidth": 1.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.3, "surface": "gravel", "speedLimitKmh": 30,
     "centreLine": "none", "edgeLines": False, "cornerRadius": 30},
    {"id": "forest_loop", "name": "točna", "class": "track", "nodes": ["FT2", "FT3", "FT4", "FT2"],
     "laneWidth": 1.75, "edgeStripWidth": 0.0, "shoulderWidth": 0.3, "surface": "gravel", "speedLimitKmh": 20,
     "centreLine": "none", "edgeLines": False, "cornerRadius": 15},
]


def node_list():
    out = []
    for nid, (x, z) in NODES.items():
        n = {"id": nid, "position": [x, z]}
        if nid in URBAN:
            n["urban"] = True
        if nid in NAMES:
            n["name"] = NAMES[nid]
        if nid in MAIN_ROADS:
            n["mainRoads"] = MAIN_ROADS[nid]
        if nid in CONTROL:
            n["control"] = CONTROL[nid]
        if nid in SIGNALS:
            n["signals"] = SIGNALS[nid]
        out.append(n)
    return out


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
    """Start point, end point, heading and length of the straight between two nodes."""
    pa, pb = NODES[a], NODES[b]
    return pa, pb, heading_deg(pb[0] - pa[0], pb[1] - pa[1]), math.hypot(pb[0] - pa[0], pb[1] - pa[1])


# ----------------------------------------------------------------------------- terrain
def terrain():
    return {
        "schemaVersion": 1,
        "size": [4200, 5800],
        "cellSize": 4,
        "baseHeight": 0,
        "roadBlendWidth": 16,
        "noise": {"amplitude": 5, "wavelength": 520, "octaves": 4, "seed": 7},
        "features": [
            {"type": "plateau", "center": [0, -100], "radius": 800, "height": 3},
            {"type": "hill", "center": [350, -2050], "radius": 750, "height": 48},
            {"type": "hill", "center": [-1350, -950], "radius": 520, "height": 22},
            {"type": "hill", "center": [-900, 950], "radius": 650, "height": -12},
            {"type": "ridge", "center": [1500, -250], "end": [2050, -1250], "radius": 320, "height": 20},
            {"type": "hill", "center": [1100, 900], "radius": 500, "height": 14},
        ],
        "regions": [
            {"type": "town", "polygon": [[-900, -1050], [850, -1050], [850, 700], [-900, 700]]},
            {"type": "forest", "polygon": FOREST_NORTH},
            {"type": "forest", "polygon": FOREST_SOUTH_EAST},
            {"type": "field", "crop": "wheat", "polygon": [[-1850, 200], [-950, 160], [-850, 900], [-1450, 1350], [-1950, 950]], "seed": 11},
            {"type": "field", "crop": "rapeseed", "polygon": [[900, -180], [1650, -560], [1950, 40], [1250, 420]], "seed": 12},
            {"type": "field", "crop": "maize", "polygon": [[-1750, -1350], [-850, -1250], [-750, -620], [-1550, -520]], "seed": 13},
            {"type": "field", "crop": "stubble", "polygon": [[-400, 1250], [500, 900], [700, 1500], [-200, 1700]], "seed": 14},
            {"type": "meadow", "polygon": [[850, -1100], [1500, -1400], [1650, -900], [1000, -700]]},
        ],
    }


FOREST_NORTH = [[-620, -1380], [150, -1280], [900, -1480], [1520, -1720], [1650, -2350], [700, -2500], [-250, -2560], [-720, -2050]]
FOREST_SOUTH_EAST = [[600, 900], [1350, 700], [1450, 1350], [750, 1450]]


# ----------------------------------------------------------------------------- objects
def buildings():
    rng = random.Random(42)
    out = []

    def house(pos, heading, kind="house", width=None, depth=None, eaves=None, floors=None, pitch=None, seed=None):
        b = {"type": kind, "position": [round(pos[0], 1), round(pos[1], 1)], "rotationDeg": round(heading % 360.0, 1)}
        b["width"] = width or round(rng.uniform(10.5, 14.0), 1)
        b["depth"] = depth or round(rng.uniform(8.5, 11.0), 1)
        b["eavesHeight"] = eaves or (6.4 if kind == "house" else 3.6)
        b["floors"] = floors or (2 if kind == "house" else 1)
        b["roofPitchDeg"] = pitch or rng.choice([35, 38, 42, 45])
        b["seed"] = seed if seed is not None else rng.randint(1, 10_000)
        out.append(b)

    def row(a, b, setback, side, spacing, kinds, start=0.06, end=0.94, skip=()):
        """Houses along the straight a->b, on the left (-1) or right (+1) side, fronts facing the road."""
        pa, pb, h, length = seg(a, b)
        n = max(1, int(length * (end - start) / spacing))
        for i in range(n):
            t = start + (end - start) * (i + 0.5) / n
            if any(lo <= t <= hi for lo, hi in skip):
                continue
            p = offset(along(pa, pb, t), h, side * setback)
            face = (h + 90.0) if side < 0 else (h - 90.0)   # facades face the road
            kind = rng.choice(kinds)
            house(p, face, kind)

    # Main street: houses on both sides between the town entrance and exit, leaving the square open.
    row("W2", "W1", 15.5, -1, 22, ["house", "house", "cottage"])
    row("W2", "W1", 15.5, +1, 22, ["house", "cottage"])
    row("W1", "SQ", 15.5, -1, 20, ["house"], end=0.72)
    row("W1", "SQ", 15.5, +1, 20, ["house"], end=0.72)
    row("SQ", "E1", 15.5, -1, 20, ["house"], start=0.30)
    row("SQ", "E1", 15.5, +1, 20, ["house", "shop"], start=0.30)
    row("E1", "E2", 15.5, -1, 22, ["house", "cottage"], start=0.08, end=0.90)
    row("E1", "E2", 15.5, +1, 22, ["cottage", "house"], start=0.08, end=0.90)
    # Square: town hall and church face the square from the north, shops on the south.
    house((-70, -62), 180, "church", width=14, depth=32, eaves=9, floors=1, pitch=50, seed=5)
    house((70, -48), 180, "hall", width=26, depth=14, eaves=9.5, floors=3, pitch=40, seed=6)
    house((-20, 46), 0, "shop", width=18, depth=12, eaves=7.2, floors=2, pitch=35, seed=7)
    house((60, 48), 0, "house", width=14, depth=11, eaves=6.8, floors=2, pitch=40, seed=8)
    house((-100, 50), 0, "house", width=13, depth=10, eaves=6.4, floors=2, pitch=42, seed=9)
    # Residential streets.
    row("W1", "RW1", 12.5, -1, 19, ["house", "cottage"], start=0.12, end=0.92)
    row("W1", "RW1", 12.5, +1, 19, ["house"], start=0.12, end=0.92)
    row("RW1", "N1", 12.5, -1, 19, ["house", "cottage"], start=0.10, end=0.90)
    row("RW1", "N1", 12.5, +1, 19, ["cottage", "house"], start=0.10, end=0.90)
    row("E1", "RE1", 12.5, -1, 19, ["house"], start=0.12, end=0.92)
    row("E1", "RE1", 12.5, +1, 19, ["house", "cottage"], start=0.12, end=0.92)
    row("RE1", "S1", 12.5, -1, 19, ["cottage", "house"], start=0.10, end=0.90)
    row("RE1", "S1", 12.5, +1, 19, ["house"], start=0.10, end=0.90)
    row("SQ", "S1", 13.5, -1, 20, ["house"], start=0.30, end=0.88)
    row("SQ", "S1", 13.5, +1, 20, ["house", "shop"], start=0.30, end=0.88)
    row("S1", "S2", 13.5, -1, 21, ["house", "cottage"], start=0.10, end=0.86)
    row("S1", "S2", 13.5, +1, 21, ["cottage"], start=0.10, end=0.86)
    row("SQ", "N1", 13.5, -1, 20, ["house"], start=0.30, end=0.88)
    row("SQ", "N1", 13.5, +1, 20, ["house"], start=0.30, end=0.88)
    row("N2", "RC1", 12.5, -1, 20, ["house", "cottage"], start=0.15, end=0.90)
    row("N2", "RC1", 12.5, +1, 20, ["house"], start=0.15, end=0.90)
    row("RC1", "E1", 12.5, +1, 21, ["house", "cottage"], start=0.10, end=0.90)
    row("RC1", "E1", 12.5, -1, 21, ["cottage", "house"], start=0.10, end=0.90)
    row("N2", "N3", 14.0, -1, 22, ["house", "cottage"], start=0.15, end=0.85)
    row("N2", "N3", 14.0, +1, 22, ["cottage"], start=0.15, end=0.85)
    # Prefab estate (paneláky) inside the block RW1-N1-N2-RN1.
    for pos, rot in [((-170, -400), 0), ((-170, -470), 0), ((-170, -540), 0), ((-260, -480), 90), ((-80, -480), 90)]:
        house(pos, rot, "block", width=42, depth=12, eaves=23.5, floors=8, pitch=8, seed=rot + pos[1])
    house((-70, -360), 180, "shop", width=22, depth=14, eaves=4.2, floors=1, pitch=12, seed=77)
    # Farm buildings at the village loop and a barn near W3.
    house((-720, -1440), 200, "barn", width=24, depth=12, eaves=5.5, floors=1, pitch=40, seed=21)
    house((-680, -1520), 20, "house", width=13, depth=10, eaves=6.0, floors=2, pitch=42, seed=22)
    house((-1045, -1235), 130, "cottage", width=11, depth=9, eaves=3.6, floors=1, pitch=45, seed=23)
    house((-1470, 340), 315, "barn", width=22, depth=11, eaves=5.2, floors=1, pitch=38, seed=24)
    house((-1530, 265), 135, "cottage", width=10, depth=8, eaves=3.4, floors=1, pitch=45, seed=25)
    # Chapel at the forest edge and a hunting lodge at the track end.
    house((-455, -1560), 60, "chapel", width=5, depth=7, eaves=4.5, floors=1, pitch=50, seed=26)
    house((-40, -2700), 120, "cottage", width=9, depth=7, eaves=3.2, floors=1, pitch=48, seed=27)
    return out


def signs_and_props():
    signs = []
    props = []

    def sign(code, node_a, node_b, t, side, code_heading_from_travel=True, text="", value=0.0, right=6.0):
        """Sign along a->b at fraction t, on the given side (+1 right of travel a->b), facing back to the drivers."""
        pa, pb, h, _ = seg(node_a, node_b)
        p = offset(along(pa, pb, t), h, side * right)
        face = (h + 180.0) % 360.0 if code_heading_from_travel else h
        s = {"code": code, "position": list(p), "headingDeg": round(face, 1)}
        if text:
            s["text"] = text
        if value:
            s["value"] = value
        signs.append(s)

    # Town boundary signs (IZ 4a entering, IZ 4b leaving) at the four town entrances.
    sign("IZ4a", "W3", "W2", 0.965, +1, text="Lipová")
    sign("IZ4b", "W2", "W3", 0.035, +1, text="Lipová")
    sign("IZ4a", "E3", "E2", 0.955, +1, text="Lipová")
    sign("IZ4b", "E2", "E3", 0.045, +1, text="Lipová")
    sign("IZ4a", "F4", "N3", 0.955, +1, text="Lipová")
    sign("IZ4b", "N3", "F4", 0.045, +1, text="Lipová")
    sign("IZ4a", "S3", "S2", 0.96, +1, text="Lipová")
    sign("IZ4b", "S2", "S3", 0.04, +1, text="Lipová")
    # Priority at the square and the church crossing.
    sign("P2", "W1", "SQ", 0.86, +1)
    sign("P2", "E1", "SQ", 0.86, +1)
    sign("P4", "N1", "SQ", 0.86, +1)
    sign("P4", "S1", "SQ", 0.86, +1)
    sign("P2", "SQ", "E1", 0.86, +1)
    sign("P2", "E2", "E1", 0.86, +1)
    sign("P4", "RE1", "E1", 0.88, +1)
    sign("P4", "RC1", "E1", 0.88, +1)
    sign("P4", "RW1", "W1", 0.88, +1)
    sign("P4", "V3", "W2", 0.975, +1)
    sign("P4", "RW1", "N1", 0.88, +1)
    sign("P4", "RN1", "N2", 0.88, +1)
    sign("P4", "RC1", "N2", 0.88, +1)
    sign("P4", "RE1", "S1", 0.88, +1)
    sign("P4", "V1", "F4", 0.95, +1)
    sign("P6", "FT1", "F3", 0.96, +1)
    sign("P1", "SQ", "N1", 0.35, +1)          # crossroads warning before the estate junction
    # Directional signs at the square.
    sign("IS3c", "W1", "SQ", 0.80, +1, text="Bor 6 / Kamenice 14")
    sign("IS3c", "E1", "SQ", 0.80, +1, text="Bor 12 / Lipová-nádraží 3")
    # Speed and hazards.
    sign("B20a", "E2", "E3", 0.50, +1, value=70)
    sign("B20b", "E3", "E4", 0.10, +1, value=70)
    sign("A14", "N3", "F4", 0.35, +1)          # deer, entering the forest
    sign("A14", "F1", "F2", 0.30, +1)
    sign("A22", "F3", "F2", 0.05, +1)          # forest road junction ahead
    sign("A7a", "F3", "FT1", 0.12, +1)         # rough surface on the track
    sign("B20a", "W2", "W1", 0.10, +1, value=30)   # calm street after the entrance
    sign("IP6", "W1", "SQ", 0.93, +1)
    sign("IP6", "SQ", "E1", 0.07, +1)
    sign("IJ4c", "SQ", "E1", 0.18, +1)
    sign("IJ4c", "SQ", "W1", 0.18, +1)

    # Bus stops at the square (shelter on the right of each direction).
    def prop(kind, node_a, node_b, t, side, right=7.5, length=0.0, scale=1.0, face_road=True):
        pa, pb, h, _ = seg(node_a, node_b)
        p = offset(along(pa, pb, t), h, side * right)
        rot = (h - 90.0) % 360.0 if side > 0 else (h + 90.0) % 360.0
        d = {"type": kind, "position": list(p), "rotationDeg": round(rot if face_road else h, 1)}
        if length:
            d["length"] = length
        if scale != 1.0:
            d["scale"] = scale
        props.append(d)

    prop("bus_stop", "SQ", "E1", 0.18, +1)
    prop("bus_stop", "SQ", "W1", 0.18, +1)
    prop("bench", "W1", "SQ", 0.95, +1, right=8.0)
    prop("bench", "SQ", "E1", 0.06, -1, right=8.0)
    for t in (0.2, 0.4, 0.6, 0.8):
        prop("lamp", "W1", "SQ", t, +1, right=6.2)
        prop("lamp", "SQ", "E1", t, -1, right=6.2)
        prop("lamp", "SQ", "N1", t, +1, right=6.0)
        prop("lamp", "SQ", "S1", t, -1, right=6.0)
    for t in (0.15, 0.35, 0.55, 0.75, 0.95):
        prop("lamp", "W2", "W1", t, +1, right=6.2)
        prop("lamp", "E1", "E2", t, -1, right=6.2)
    prop("gate", "F3", "FT1", 0.06, 0, right=0.0, face_road=False)
    prop("timber_stack", "FT1", "FT2", 0.35, +1, right=6.0, length=12)
    prop("timber_stack", "FT1", "FT2", 0.60, -1, right=6.0, length=9)
    prop("fence", "V1", "V2", 0.08, +1, right=7.0, length=60)
    prop("wall", "W1", "SQ", 0.40, -1, right=10.0, length=40)
    return signs, props


def objects():
    signs, props = signs_and_props()
    return {
        "schemaVersion": 1,
        "buildings": buildings(),
        "props": props,
        "signs": signs,
        "trees": [
            {"species": "linden", "position": [-30, 30], "scale": 1.3, "seed": 1},
            {"species": "linden", "position": [30, 30], "scale": 1.25, "seed": 2},
            {"species": "linden", "position": [-30, -30], "scale": 1.2, "seed": 3},
            {"species": "oak", "position": [-120, -95], "scale": 1.4, "seed": 4},
            {"species": "birch", "position": [140, 40], "scale": 1.0, "seed": 5},
            {"species": "linden", "position": [-1490, 300], "scale": 1.5, "seed": 6},
            {"species": "oak", "position": [-660, -1450], "scale": 1.3, "seed": 7},
        ],
        "forests": [
            {"polygon": FOREST_NORTH, "density": 0.022, "margin": 7,
             "species": [{"species": "spruce", "weight": 0.62}, {"species": "pine", "weight": 0.2}, {"species": "beech", "weight": 0.18}], "seed": 3},
            {"polygon": FOREST_SOUTH_EAST, "density": 0.014, "margin": 8,
             "species": [{"species": "oak", "weight": 0.5}, {"species": "birch", "weight": 0.3}, {"species": "pine", "weight": 0.2}], "seed": 4},
            {"polygon": [[-1700, -1400], [-1300, -1450], [-1250, -1050], [-1650, -1000]], "density": 0.018, "margin": 6,
             "species": [{"species": "spruce", "weight": 0.5}, {"species": "beech", "weight": 0.5}], "seed": 5},
        ],
        "avenues": [
            {"road": "main", "fromNode": "E2", "toNode": "E4", "species": "linden", "spacing": 20, "offset": 3.0, "left": True, "right": True, "seed": 1},
            {"road": "south", "fromNode": "SW1", "toNode": "S3", "species": "maple", "spacing": 18, "offset": 2.8, "left": True, "right": True, "seed": 2},
            {"road": "village", "fromNode": "V2", "toNode": "V3", "species": "birch", "spacing": 16, "offset": 2.5, "left": True, "right": False, "seed": 3},
        ],
    }


def traffic():
    def spawn(name, a, b, t, lane_offset=1.5):
        pa, pb, h, _ = seg(a, b)
        p = offset(along(pa, pb, t), h, lane_offset)
        return {"name": name, "position": list(p), "headingDeg": round(h, 1)}

    return {
        "schemaVersion": 1,
        "playerSpawns": [
            spawn("square", "W1", "SQ", 0.62),
            spawn("forest", "N3", "F4", 0.5, 1.4),
            spawn("fields", "SW1", "S3", 0.5, 1.4),
            spawn("east", "E3", "E2", 0.5, 1.5),
        ],
        "densityPerKm": 1.2,
        "maxVehicles": 20,
        "vehicles": ["lipan_12"],
        "spawnMinDistance": 70,
        "despawnDistance": 650,
    }


def map_doc():
    return {
        "schemaVersion": 1,
        "id": "lipova",
        "displayName": "Lipová",
        "description": "Fictional small town in the Czech countryside: square, residential streets, prefab estate, "
                       "fields with tree avenues, spruce forest with a forest road and a gravel track. About 18 km of roads.",
        "author": "cna-car-simulator contributors",
        "license": "MIT (see LICENSE); all content is procedural or authored in this repository",
        "files": {"terrain": "terrain.json", "roads": "roads.json", "objects": "objects.json", "traffic": "traffic.json"},
    }


def dump(name, data):
    path = OUT / name
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {path.relative_to(OUT.parents[2])} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    dump("map.json", map_doc())
    dump("terrain.json", terrain())
    dump("roads.json", {"schemaVersion": 1, "nodes": node_list(), "roads": ROADS})
    dump("objects.json", objects())
    dump("traffic.json", traffic())
