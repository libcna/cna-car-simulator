# Phase 14 visual comparisons (2026-09-24)

The original pairs are 1280 × 720 captures from CNA OPENGLES3 on the Debian 13 desktop's AMD Radeon
780M (Mesa 25.0.7), with a fixed camera and clock for each pair. JPEG quality 88 was used
for scene frames; the instrument texture remains PNG. The JPEGs are review evidence, not
texture assets shipped by the simulator. The road-repair **before** frame used OPENGL33 on
the same Radeon while the older code was built there; the Phase 14 renderer comparison found
OPENGL33 and OPENGLES3 screenshots byte-identical at the fixed town and cockpit scenes. The
gait pair uses the hidden 800 × 480 Radeon path described below.

| Pair | What changed | Fixed view / scene |
| --- | --- | --- |
| [Town before](town-before.jpg) / [after](town-after.jpg) | Framed and panelled doors, quieter plaster variation | `--view -78 9 -30 50 -6`, town |
| [Pedestrian before](pedestrian-before.jpg) / [after](pedestrian-after.jpg) | Rounded shared body geometry, head details, hands, varied hair | `--spawn square --view -61 6.5 14 0 -4`, two frames |
| [Clothing before](pedestrian-clothes-before.jpg) / [after](pedestrian-clothes-after.jpg) | Reusable longer coat and brimmed cap silhouettes with muted outerwear colours | Same square view, clear 13:00, two frames |
| [Gait before](offscreen-800-scene-square_people.png) / [after](offscreen-800-pedestrian-gait-after.png) | Small body rise/sway, levelled shoes and flared coat hem; 44 pedestrian submissions in both captures | Hidden Radeon 800 × 480, square view, clear 13:00, frame 120 |
| [Fog before](fog-before.jpg) / [after](fog-after.jpg) | Distant tree batches culled at dense-fog visibility range | Same fixed town camera and fog preset |
| [Cockpit before](cockpit-before.jpg) / [after](cockpit-after.jpg) | Steering wheel position, material separation and dashboard visibility | Stationary noon cockpit at the square |
| [Cluster before](cluster-before.png) / [after](cluster-after.png) | Fewer colliding speed labels; odometer and trip fit the centre screen | Same 420 km/h instrument scale |
| [Road before](road-repair-before.jpg) / [after](road-repair-after.jpg) | Sparse resurfaced utility cuts in the existing asphalt mesh | `--view -145 7 4 90 -25`, 13:00, two frames |
| [Church before](church-before.png) / [after](church-after.png) | Square-facing tower pilasters, circular window and stone entrance portal | `--view -78 9 -30 50 -6`, 13:00, scattered cloud, 40 frames |
| [Forest before](forest-before.png) / [after](forest-after.png) | Two seeded crown silhouettes per species in one tree-card atlas; narrower alternate spruce with more visible trunk | `--spawn forest --view -228 5 -1280 0 -4`, clear 13:00, 40 frames |
| [Square before](square-planters-before.jpg) / [after](square-planters-after.jpg) | Two low stone beds with shrubs frame the memorial; the open cobbled centre remains usable | `--spawn square --view -55 7 -65 180 -7`, clear 13:00, two frames |
| [Shopfront before](offscreen-800-shopfront-before.png) / [after](offscreen-800-shopfront-after.png) | Shop bay piers, continuous fascia and seeded canopy or ledge; 31 shops gain 2,076 nominal triangles across the full map without another material batch | Hidden Radeon 800 × 480, `--spawn square --view -88 7 -61 180 -3`, clear 13:00, two frames |
| [Shop signs before](shop-sign-before.png) / [after](shop-sign-after.png) | Four seeded Czech shop names on shallow pale fascia boards; the adjacent glass, doors and building footprints are unchanged | Hidden Radeon 800 × 480, `--spawn square --view -45 5 -43 90 -3`, clear 13:00, two frames |
| [Forest ground before](offscreen-800-scene-forest.png) / [after](offscreen-800-forest-ground-after.png) | Subdued moss/needle-litter patches and a roughly 6 m forest/meadow colour transition; fixed pixels change only along the ground at the treeline | Hidden Radeon 800 × 480, `--spawn forest --view -228 5 -1280 0 -4`, clear 13:00, frame 120 |
| [Rural verge before](road-verge-clear-before.png) / [after](road-verge-clear-after.png) | Bilinear sampling of the road-distance ground tint removes five-metre steps beside the road | Hidden Radeon 800 × 480, `--spawn forest --view -228 6 -1235 327 -10`, clear 13:00, frame 2 |
| [Snow verge before](road-verge-snow-before.png) / [after](road-verge-snow-after.png) | The grass verge now retains field-level snow cover; gravel keeps more than worn asphalt | Same hidden Radeon view, snow 13:00, frame 2 |

Additional checks: [night cockpit](cockpit-night.jpg), [walking camera](walking.jpg),
[helicopter aerial view](flight.jpg), [road repair in rain](road-repair-rain.jpg),
[road repair under snow](road-repair-snow.jpg), [forest before snow-crown work](forest-snow.png) /
[after](forest-snow-after.png),
and [B 21a beside the restricted main road](overtaking-sign.jpg).
The [closer shopfront view](offscreen-800-shopfront-detail-after.png) checks the new
horizontal silhouette along a square frontage; parked vehicles partly occlude the glazing.
The [closer sign inspection](shop-sign-detail.png) shows the `ELEKTRO` board above the
parked cars. In the matched 800 × 480 shop view, 359 pixels change above a 12/255
channel threshold, all in the upper building half. The signs use the existing light
frame and dark trim batches.
The same forest-ground view under [snow before](offscreen-800-scene-forest_snow.png) /
[after](offscreen-800-forest-ground-snow-after.png) shows that the snow surface still covers
the transition. A [closer interior view](offscreen-800-forest-floor-interior-after.png)
checks the mottled floor under the canopy; undergrowth remains sparse.
The road-verge pass also has a [rain check](road-verge-rain-after.png). The clear and snow
before frames above were captured from the clean pushed HEAD with the exact same fixed
camera; the after frames use the final code. The snow change is plainly visible without
adding terrain or verge geometry.
The next winter-atlas step makes the low roadside shrubs lighter on top: compare the
[snow-verge result before this step](road-verge-snow-after.png) with the
[frosted bushes](forest-bush-snow-after.png). The clear version of the same view remains
pixel-identical; the shrub atlas keeps its dimensions and the lower foliage stays dark.
The forest snow pair checks the gradual upper-crown tint and the unchanged card silhouettes;
further branch geometry and deciduous winter shape work remain. The cluster is shown while stationary with its engine
off, so it checks layout rather than gauge motion. The night frame likewise checks cabin
visibility; live illumination under every weather combination still needs review.

These are incremental improvements. The town still needs more regional facade and street
detail, pedestrians still need broader clothing variety and moving-camera review, and the cockpit needs a broader
production-quality geometry/material pass. The aerial frame is useful for spotting culling
and density problems as Phase 14 proceeds.

## Hidden Radeon benchmark captures (800 × 480)

These frames came from SDL offscreen plus surfaceless EGL on the same Radeon 780M; no
window appeared on the physical desktop. They use a matched 800 × 480 logical and EGL
surface, because the first hidden 1280 × 720 trial clipped to 800 × 480 and was discarded.
The simulator version is unchanged between the mirror variants; only benchmark options differ.

The later [paired all-mirror baseline](offscreen-800-mirror-paired-all.png) and
[wing-visibility-cull result](offscreen-800-wing-cull-after.png) are pixel-identical in
the fixed rainy-night cockpit. A [rightward cockpit look](offscreen-800-wing-cull-right-look.png)
checks that the passenger-side reflection is rendered again when the glass enters view.

| Mirror isolation | Capture |
| --- | --- |
| All mirrors | [rainy night cockpit](offscreen-800-mirror-all.png) |
| No mirrors | [rainy night cockpit](offscreen-800-mirror-none.png) |
| Rear only at 384 × 100 | [rainy night cockpit](offscreen-800-mirror-rear384.png) |
| Rear only, every second frame | [rainy night cockpit](offscreen-800-mirror-rear_every2.png) |

Fixed scene captures: [clear town](offscreen-800-scene-town_clear.png),
[town cockpit](offscreen-800-scene-town_cockpit.png),
[forest](offscreen-800-scene-forest.png),
[snow forest](offscreen-800-scene-forest_snow.png),
[fog square](offscreen-800-scene-town_fog.png),
[square pedestrians](offscreen-800-scene-square_people.png),
[walking](offscreen-800-scene-walking.png), the [bound return-to-car walking hint](walking-bound-return-hint.png), and
[helicopter aerial view](offscreen-800-scene-flight.png).
The aerial camera looks across the town from 95 m altitude; an earlier nearly vertical
view was discarded as unrepresentative of aerial rendering cost.

## Cockpit geometry review

The hidden Radeon 800 × 480 review used the same town-route spawn, stationary car, 60 seconds
of traffic warm-up and two rendered frames for every image. The [noon before](cockpit-review-before-noon.png) /
[after](cockpit-review-after-noon.png), [night before](cockpit-review-before-night.png) /
[after](cockpit-review-after-night.png), and [rainy night before](cockpit-review-before-rainy_night.png) /
[after](cockpit-review-after-rainy_night.png) pairs show the instrument face moved in front of
the dashboard fascia. The entire dial and lower readout are now visible within a slimmer
binnacle. The steering wheel sits slightly lower and farther forward; satin trim separates
its spokes, radio surround, vent surrounds and instrument rim from the dark textured pad.

The final cockpit was also inspected at [sunset](cockpit-review-after-sunset.png),
[daylight rain](cockpit-review-after-rain.png), [fog](cockpit-review-after-fog.png), and
[snow](cockpit-review-after-snow.png). The instrument face remains legible in all seven
conditions; the night material contrast and larger cabin surfaces still need refinement.

A later material pass gives the windshield A-pillars matte charcoal trim while leaving the
roof lining pale. Matched hidden Radeon [noon before](cockpit-pillar-before-noon.png) /
[after](cockpit-pillar-after-noon.png), [night before](cockpit-pillar-before-night.png) /
[after](cockpit-pillar-after-night.png), and [rainy-night before](cockpit-pillar-before-rainy_night.png) /
[after](cockpit-pillar-after-rainy_night.png) frames show the separation. The final variant
was also checked at [sunset](cockpit-pillar-after-sunset.png),
[rain](cockpit-pillar-after-rain.png), [fog](cockpit-pillar-after-fog.png), and
[snow](cockpit-pillar-after-snow.png). The wide physical pillar and broad dashboard pad remain
open visual-quality work; this change only assigns existing inward-facing triangles to a
different existing interior material.

The subsequent geometry pass replaced those broad inward-facing skin quads with a narrow
trim lip following both windshield rails. In a matched HUD-free noon pair,
[before](cockpit-narrow-pillar-before-nohud.png) /
[after](cockpit-narrow-pillar-after-nohud.png), 14,455 pixels change in the left
front-frame crop (x 100–309, y 0–329; channel difference above 12). The town frontage
is visible through the newly open side of the windshield. Final hidden Radeon views are
[noon](cockpit-narrow-pillar-noon.png), [sunset](cockpit-narrow-pillar-sunset.png),
[night](cockpit-narrow-pillar-night.png), [rain](cockpit-narrow-pillar-rain.png),
[fog](cockpit-narrow-pillar-fog.png), [snow](cockpit-narrow-pillar-snow.png), and
[rainy night](cockpit-narrow-pillar-rainy_night.png). The exterior body and glass meshes
are unchanged; a chase-camera inspection showed the car remains closed. The broad lower
dashboard and finer night material separation remain open.

## Facade shutter variants

A fixed road-facing view compares a town house [before](facade-shutters-house-before.png) /
[after](facade-shutters-house-after.png), and a one-storey village cottage
[before](facade-shutters-cottage-before.png) /
[after](facade-shutters-cottage-after.png). All four are hidden Radeon 800 × 480 frames
with the same camera, clear noon weather and 60 seconds of deterministic traffic warm-up
within each pair. The selected house swaps its more elaborate plaster window surrounds
for dark timber shutters; the cottage gains shutters beside its existing windows.
The window surfaces stay uncovered. In the two matched frames, 2,975 and 3,983 pixels
respectively change around the windows at a channel-difference threshold of 12.

## Arched instrument hood

The 1280 × 720 dedicated virtual-display [before](cockpit-arched-hood-before.png) /
[after](cockpit-arched-hood-after.png) pair uses the same stationary town-route cockpit.
Five loft sections taper the previously flat hood toward the dashboard on both sides.
At a channel difference above 12/255, 8,457 pixels change in the hood; the full dial
face and the road above it are unchanged. The HUD and exterior scene remain aligned.

Final hidden Radeon 800 × 480 views check [noon](cockpit-arched-hood-noon.png),
[sunset](cockpit-arched-hood-sunset.png), [night](cockpit-arched-hood-night.png),
[rain](cockpit-arched-hood-rain.png), [fog](cockpit-arched-hood-fog.png),
[snow](cockpit-arched-hood-snow.png) and
[rainy night](cockpit-arched-hood-rainy_night.png). The dials remain readable in every
view. This is an incremental binnacle shape improvement; the wide dashboard surfaces
and finer night materials remain open under P14-022.

## Passenger dashboard pad and lid

The passenger side now has a shallow cloth-grain top pad, a short bevel and a recessed
soft-touch airbag lid above the glovebox. The fixed 800 × 480 hidden Radeon noon
[before](cockpit-passenger-pad-before.png) / [after](cockpit-passenger-pad-after.png)
pair changes 2,268 pixels above a 12/255 channel threshold, all below image row 300;
the gauges and view of the road stay in place. The lid is easier to see in the
[HUD-free view](cockpit-passenger-pad-nohud.png); the normal speed HUD partly covers it.

The final variant was checked at [noon](cockpit-passenger-pad-noon.png),
[sunset](cockpit-passenger-pad-sunset.png), [night](cockpit-passenger-pad-night.png),
[rain](cockpit-passenger-pad-rain.png), [fog](cockpit-passenger-pad-fog.png),
[snow](cockpit-passenger-pad-snow.png) and
[rainy night](cockpit-passenger-pad-rainy_night.png). The cluster is readable in all
seven frames. These are stationary scene checks; broader cabin shape and night material
work remain under P14-022.

## Soft dashboard material and cowl outlets

Two shallow defroster outlets now break up the windscreen cowl, and the existing
passenger pad and recessed airbag lid use a separate soft cloth-grain material from
the seats. In the matched 800 × 480 hidden Radeon [before](cockpit-soft-cowl-before.png) /
[after](cockpit-soft-cowl-noon.png) pair, 3,881 pixels change above a 12/255 channel
threshold, all on the passenger dash below image row 324. The dial face and road stay
aligned. The final variant was also captured at
[sunset](cockpit-soft-cowl-sunset.png), [night](cockpit-soft-cowl-night.png),
[rain](cockpit-soft-cowl-rain.png), [fog](cockpit-soft-cowl-fog.png),
[snow](cockpit-soft-cowl-snow.png) and
[rainy night](cockpit-soft-cowl-rainy_night.png). The low cabin remains dark at night,
so stronger night material separation is still open under P14-022.

## Charcoal inner windscreen rail

The inward-facing roof-rail strip beside the narrow A-pillar lip now uses the same
matte charcoal trim as that lip. The main headliner remains pale, and the exterior
shell has not moved. Against the previous [noon frame](cockpit-soft-cowl-noon.png),
the matched [new noon frame](cockpit-charcoal-rail-noon.png) changes 4,772 pixels above
a 12/255 channel threshold, concentrated on the sloping left pillar and its far-side
counterpart. The road and gauges remain aligned. Final hidden Radeon views cover
[sunset](cockpit-charcoal-rail-sunset.png), [night](cockpit-charcoal-rail-night.png),
[rain](cockpit-charcoal-rail-rain.png), [fog](cockpit-charcoal-rail-fog.png),
[snow](cockpit-charcoal-rail-snow.png) and
[rainy night](cockpit-charcoal-rail-rainy_night.png). The lower controls still need a
stronger night pass, so P14-022 remains open.

## Walking heading and return review

The original [forest entry](offscreen-800-walking-forest-review.png) faced across the
meadow because the walking yaw mirrored the car's horizontal forward component. The
[corrected entry](walking-forest-heading-after.png) faces along the road from the same
spawn, time and weather. A dedicated virtual-display
[long route frame](walking-forest-route-telemetry.png) records the walker 7.3 m from the
parked car, stopped after forward input, with the road still in view. The position row
and moving/stopped state are from the F3 overlay. A separate virtual-display
[return frame](walking-return-square.png) follows an actual `G` keypress at the square;
the log confirms entry on foot and return to the car. The focused walking regressions
cover angled headings, kerbs, proximity and complete traffic-body overlap.

The final route review ran only on a dedicated 800 × 480 Xvfb display. At the hrad spawn,
the [stopped hill route](walking-hill-route-final.png) ended 9.5 m from the car and roughly
1 m higher, without a camera jump. At the square, traffic was warmed for 40 simulated
seconds (20 live vehicles), then the walker sidestepped past the parked car and continued
beside the road. The [stopped traffic route](walking-traffic-route-final.png) ended 8.2 m
from the car with the camera settled and traffic still active. The
[final entry overlay](walking-traffic-overlay-final.png) names the on-foot diagnostic
section correctly. These runs and the focused collision/slope/proximity regressions close
P14-023.

## Sparse forest undergrowth

A few low shrubs now occupy gaps among the forest trees, using the existing bush cards
and their snow atlas. In the fixed virtual OPENGL33 interior view, the
[clear before](forest-undergrowth-clear-before.png) / [after](forest-undergrowth-clear-after.png)
pair changes 583 pixels above a 12/255 channel threshold, concentrated on a single
visible ground patch. The matched [snow before](forest-undergrowth-snow-before.png) /
[after](forest-undergrowth-snow-after.png) pair changes 612 pixels; the small shrub
receives muted winter cover. Final hidden Radeon [clear](forest-undergrowth-radeon-clear.png)
and [snow](forest-undergrowth-radeon-snow.png) frames confirm its colour on hardware.
The wider forest-edge camera did not show a change because nearby crowns hid the low
plants. This is a sparse interior layer; deeper forest-floor variety remains open.

## Rural verge edge

The grass/soil transition outside the fixed gravel shoulder now varies smoothly with
road station instead of forming a ruler-straight two-metre line. It remains between
1.25 and 2.75 m from the shoulder and shares the existing strip vertices, so lane and
collision geometry, submissions and triangle count do not change. In the matched hidden
Radeon forest-road view, [clear before](verge-edge-clear-before.png) /
[after](verge-edge-clear-after.png) changes 2,858 pixels above a 12/255 channel threshold.
[Snow before](verge-edge-snow-before.png) / [after](verge-edge-snow-after.png) changes
1,311 pixels, and the [rain frame](verge-edge-rain-after.png) keeps the wet road and
outer ground continuous. Fixed-scene counts are 450 draws / 529,749 triangles in clear
weather and 622 / 440,919 in snow, unchanged from baseline. Forest-floor and wider
road-surface variety remain open under P14-021.

## Limewashed cottage frontage

A seeded group of village cottages now has pale window surrounds, corner strips and
an eaves frieze. The fixed hidden Radeon 800 × 480 [before](cottage-stucco-before.png) /
[after](cottage-stucco-after.png) pair uses `--view -659 5 78 0 -3`, clear 13:00 and
two lockstep frames. It changes 3,101 pixels above 12/255, concentrated on the cottage
facade. Shutters remain a separate variant; windows, doors, roofs, footprint and
collision geometry retain their positions. [OpenGL33](cottage-stucco-opengl33.png),
[GLES3](cottage-stucco-opengles3-xvfb.png), [software](cottage-stucco-software.png),
and [Vulkan](cottage-stucco-vulkan.png) virtual-display frames all show the same
frontage. Broader town frontage and square layout work remain under P14-020.

## Forest fog trunk alignment and mixed weather

At `--view -228 5 -1280 0 -4`, dense fog first removed distant crowns but left a
bare trunk chunk on the left. The matched hidden Radeon
[before](forest-fog-trunks-before.png) / [after](forest-fog-trunks-after.png) pair
changes 5,783 pixels above 12/255, almost wholly in that distant band. The nearby
trees and ground remain, and a clear-weather cockpit comparison confirms no change
above row 356 outside fog. Four virtual renderer checks are in
[renderer conformance](../../renderer-conformance.md).

Settled 120-frame hidden Radeon [night fog with headlamps](forest-night-fog-cockpit.png)
and [sunset snow](forest-sunset-snow-cockpit.png) show the road and cluster at the
forest spawn; their project draw measurements are in [performance notes](../../performance.md).
The light-coloured triangular opening by the left windscreen frame appears in clear
weather too, so its shape remains a P14-022 cockpit item rather than a winter artifact.

## Radio readout and cockpit lighting

The existing centre radio glass now carries a restrained green 101.2 MHz segment
readout and two short status strokes. Matched isolated-display 1280 × 720
[noon before](cockpit-radio-1280-before-noon.png) /
[after](cockpit-radio-1280-after-noon.png) and
[night before](cockpit-radio-1280-before-night.png) /
[after](cockpit-radio-1280-after-night.png) pairs each change exactly 480 pixels
above a 12/255 channel threshold, confined to x845–945/y638–662 inside the radio.
Road, cluster and dashboard silhouette are identical. The final hidden Radeon
800 × 480 set covers [noon](cockpit-radio-noon.png),
[sunset](cockpit-radio-sunset.png), [night](cockpit-radio-night.png),
[rain](cockpit-radio-rain.png), [fog](cockpit-radio-fog.png),
[snow](cockpit-radio-snow.png) and
[rainy night](cockpit-radio-rainy_night.png). The large speed HUD obscures part of
the radio at 800 × 480; the 1280 pair shows its actual in-world appearance.
The seven captures were made through hidden SDL offscreen EGL on Radeon 780M,
verified as `radeonsi`; the desktop display was not used. The remaining cockpit
shape and night material work keep P14-022 open.

## Small groups in the forest undergrowth

Some of the existing low forest shrubs now have a smaller neighbour 1.4–2.6 m away.
The original plant keeps its position, size and collider state; companions use the
same polygon, road, building and terrain clearance and remain decorative. The matched
hidden Radeon 800 × 480 view `--view -243 5 -2835 180 -4` shows
[clear before](forest-clump-before-clear.png) /
[after](forest-clump-after-clear.png) and
[snow before](forest-clump-before-snow.png) /
[after](forest-clump-after-snow.png). The only changed object is the lower plant to
the right of the original; 548 clear and 658 snow pixels exceed 12/255. Its leaves
use the existing winter atlas. Four virtual-renderer winter images are in
[renderer conformance](../../renderer-conformance.md). The wider forest floor still
needs variation under P14-021.

## Paired mirror cull result

The controlled hidden Radeon rainy-night [unculled](wing-cull-ab-unculled.png) /
[culled](wing-cull-ab-all.png) frames are byte-identical. The new benchmark
counter shows 162.289 fewer 3D draws and 387,785 fewer submitted triangles per
frame when the invisible right wing view is skipped. The left mirror remains live;
the right view refreshes when brought into the cockpit frustum. Timing and RSS
limits are recorded in [performance notes](../../performance.md).

## First-floor town-house balconies

A seeded subset of the existing two-storey and taller houses has shallow concrete
sills and slim metal Juliet rails below two first-floor windows. In the fixed
square-facing hidden Radeon 800 × 480 view (`--view -46 5 -58 0 0`), the matched
[clear before](house-balcony-before-clear.png) /
[after](house-balcony-after-clear.png) pair changes 427 pixels above a 12/255
channel threshold, limited to the two rails on the pale house. The matched
[snow before](house-balcony-before-snow.png) /
[after](house-balcony-after-snow.png) pair changes 263 pixels at the same windows;
the [rain view](house-balcony-after-rain.png) retains the same silhouette. The
balconies use the existing concrete and metal material batches, stay within the
frontage and add no collision geometry. The four virtual-renderer views are in
[renderer conformance](../../renderer-conformance.md). P14-020 remains open for
wider frontage and public-space variety.
