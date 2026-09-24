# Phase 14 visual comparisons (2026-09-24)

These are 1280 × 720 captures from CNA OPENGLES3 on the Debian 13 desktop's AMD Radeon
780M (Mesa 25.0.7), with a fixed camera and clock for each pair. JPEG quality 88 was used
for scene frames; the instrument texture remains PNG. The JPEGs are review evidence, not
texture assets shipped by the simulator. The road-repair **before** frame used OPENGL33 on
the same Radeon while the older code was built there; the Phase 14 renderer comparison found
OPENGL33 and OPENGLES3 screenshots byte-identical at the fixed town and cockpit scenes.

| Pair | What changed | Fixed view / scene |
| --- | --- | --- |
| [Town before](town-before.jpg) / [after](town-after.jpg) | Framed and panelled doors, quieter plaster variation | `--view -78 9 -30 50 -6`, town |
| [Pedestrian before](pedestrian-before.jpg) / [after](pedestrian-after.jpg) | Rounded shared body geometry, head details, hands, varied hair | `--spawn square --view -61 6.5 14 0 -4`, two frames |
| [Fog before](fog-before.jpg) / [after](fog-after.jpg) | Distant tree batches culled at dense-fog visibility range | Same fixed town camera and fog preset |
| [Cockpit before](cockpit-before.jpg) / [after](cockpit-after.jpg) | Steering wheel position, material separation and dashboard visibility | Stationary noon cockpit at the square |
| [Cluster before](cluster-before.png) / [after](cluster-after.png) | Fewer colliding speed labels; odometer and trip fit the centre screen | Same 420 km/h instrument scale |
| [Road before](road-repair-before.jpg) / [after](road-repair-after.jpg) | Sparse resurfaced utility cuts in the existing asphalt mesh | `--view -145 7 4 90 -25`, 13:00, two frames |
| [Church before](church-before.png) / [after](church-after.png) | Square-facing tower pilasters, circular window and stone entrance portal | `--view -78 9 -30 50 -6`, 13:00, scattered cloud, 40 frames |
| [Forest before](forest-before.png) / [after](forest-after.png) | Two seeded crown silhouettes per species in one tree-card atlas; narrower alternate spruce with more visible trunk | `--spawn forest --view -228 5 -1280 0 -4`, clear 13:00, 40 frames |
| [Square before](square-planters-before.jpg) / [after](square-planters-after.jpg) | Two low stone beds with shrubs frame the memorial; the open cobbled centre remains usable | `--spawn square --view -55 7 -65 180 -7`, clear 13:00, two frames |

Additional checks: [night cockpit](cockpit-night.jpg), [walking camera](walking.jpg),
[helicopter aerial view](flight.jpg), [road repair in rain](road-repair-rain.jpg),
[road repair under snow](road-repair-snow.jpg), [forest atlas under snow](forest-snow.png),
and [B 21a beside the restricted main road](overtaking-sign.jpg).
The snow frame checks that the atlas has no transparent seams; the crowns still need
weather-specific snow accumulation. The cluster is shown while stationary with its engine
off, so it checks layout rather than gauge motion. The night frame likewise checks cabin
visibility; live illumination under every weather combination still needs review.

These are incremental improvements. The town still needs more regional facade and street
detail, pedestrians still need finer clothing and gait work, and the cockpit needs a broader
production-quality geometry/material pass. The aerial frame is useful for spotting culling
and density problems as Phase 14 proceeds.
