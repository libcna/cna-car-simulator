# Vehicle materials

Every car surface is drawn with stock XNA 4.0 effects: `BasicEffect` (lit, textured, optional
vertex colour) and `EnvironmentMapEffect` (paint, chrome, glass, mirror glass) with the shared
64 px sky cube map built at start-up from the lighting rig. There are no custom shaders. The
material of a part is a `CarMaterial` slot; `VehicleRenderer` maps each slot to a *look*
(diffuse, specular, specular power, emissive, environment amount) and a texture. All textures
are procedural CPU images (`CarTextures.cpp`, `ProceduralTextures.cpp`) uploaded once.

| Slot | Effect | Texture | Look | Where |
| --- | --- | --- | --- | --- |
| `Paint` | EnvironmentMap, fresnel 2.2 | paint detail (UV space: shut lines, sill/arch darkening, fuel flap, fine grain) | body colour, specular 0.7 / 48, env 0.22 | body skin, bumpers, mirror housings, handles |
| `Glass` | EnvironmentMap from outside (env 0.45, fresnel 1.2); BasicEffect from inside | premultiplied tint with frit bands; alpha 0.62 outside, 0.20 inside | tint 0.10/0.13/0.16, specular 0.5 / 90 | windshield, side and rear glass (drawn last, `CullNone`) |
| `BlackTrim` | BasicEffect | white | 0.05 grey, specular 0.12 / 10 | lower bumper skins, sills, arch liners, wipers, antenna, underbody |
| `GlossBlack` | BasicEffect | white | 0.025 grey, specular 0.9 / 70 | B-pillars, window channels, dashboard gloss inserts |
| `Chrome` | EnvironmentMap, fresnel 0 | white | 0.62 grey, specular 1.0 / 80, env 0.8 | badges, exhaust tips, knob caps, interior mirror frame |
| `MirrorGlass` | EnvironmentMap, fresnel 0 | white | 0.22 grey, specular 1.0 / 90, env 0.45 | door mirror glass (dark reflective, not a white slab) |
| `Tyre` | BasicEffect | tread (circumferential grooves, sidewall ring) | 0.95 (texture carries the tone), specular 0.06 / 8 | tyres |
| `Rim` | BasicEffect | rim finish (machined face, dark pockets) | 0.78 grey, specular 0.9 / 44 | alloy wheels |
| `BrakeDisc` | BasicEffect | white | 0.30 grey, specular 0.5 / 30 | brake discs, hubs (LOD 0/1 only) |
| `Grille` | BasicEffect | hexagonal mesh (bars light, openings dark) | white x texture, specular 0.25 / 20 | grille, intake, rear diffuser |
| `LampHead` | BasicEffect | projector lens (two reflector bowls, ribs) | off 0.52/0.54/0.58 (glass over a grey reflector); lit 0.92/0.93/0.95 with emissive 0.78 low beam, 1.0 high beam; specular 1.0 / 80 | headlamp decals |
| `LampTail` | BasicEffect | ribbed lens | 0.55/0.03/0.03, specular 0.55 / 50; emissive 0.38 red with lights, 0.95 red braking | tail lamp decals |
| `LampIndicator` | BasicEffect | ribbed lens | 0.90/0.50/0.10; emissive amber when blinking (per side) | front and rear indicators, side repeaters |
| `LampReverse` | BasicEffect | ribbed lens | 0.82 grey; emissive when reversing | narrow inner segment of the tail cluster |
| `Plate` | BasicEffect (lit textured) | rendered plate (per car, cached) | white, specular 0.3 / 20 | front and rear plates |
| `Cluster` | BasicEffect (lit textured) | instrument cluster render target | white, emissive 0.55 with ignition (0.15 off) | cluster face |
| `Needle` | BasicEffect | white | red, emissive with ignition | (legacy needle parts; the cluster texture carries the needles) |
| `Interior` | BasicEffect (interior lighting: ambient 0.50/0.51/0.55) | plastic grain | interior colour x 1.15, specular 0.06 / 8 | dashboard top pad, door cards, console, column, floor, tailgate inner |
| `InteriorMid` | BasicEffect (interior lighting) | plastic grain | interior colour x 2 + 0.04, specular 0.05 / 8 | lower dashboard, lower door panels, armrests |
| `InteriorLight` | BasicEffect (interior lighting) | headliner weave | 0.62/0.62/0.60, specular 0.02 / 4 | headliner, pillars above the belt, sun visors |
| `Fabric` | BasicEffect (interior lighting) | cloth weave with patches | 0.21/0.21/0.23 dark grey, specular 0.03 / 4 | seats, rear bench, head restraints |
| `Vent` | BasicEffect (interior lighting) | slats | white x texture, specular 0.15 / 12 | dashboard vents |

Lamp glows: lit lamps also draw an additive radial billboard (`VehicleRenderer::DrawLampGlows`,
sizes 0.55 m headlamp, 0.45 m brake, 0.32 m tail, 0.30 m indicator/reverse) with a facing fade;
this is the only additive pass on the car.

Shadows: the sun shadow is a stencil-free convex hull of the exterior projected along the sun
with a 12 cm penumbra rim, plus a soft contact shadow under the footprint, both drawn with
`BasicEffect` (vertex colour x texture alpha, premultiplied alpha blend, depth read only). See
`ShadowGeometry.hpp`.

## Lighting exposure

The rig (`LightingRig.hpp`) is set so a sunlit horizontal surface receives about 1.0 in total
(sun 0.98/0.93/0.84 at 48 degrees elevation, ambient 0.21/0.23/0.28, sky fill 0.15/0.18/0.24,
ground bounce 0.10/0.09/0.07): textures keep their contrast instead of clipping to white. The
interior effect uses a brighter ambient (0.50/0.51/0.55) because the cabin receives no baked
occlusion and would otherwise read black.

## Traffic

Traffic cars share `VehicleMaterials`; each of the ten style models keeps its own paint detail
and glass textures, and the paint colour is overridden per car from the ten-colour palette
(`TrafficRenderer::PaintColour`). LOD 1 drops small detail parts, LOD 2 also drops chrome,
mirror glass, gloss trim, grille, brake discs, plates, glass and the cabin block.
