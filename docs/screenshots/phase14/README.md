# Phase 14 visual comparisons (2026-09-24)

These are 1280 × 720 captures from CNA OPENGLES3 on the Debian 13 desktop's AMD Radeon
780M (Mesa 25.0.7), with a fixed camera and clock for each pair. JPEG quality 88 was used
for scene frames; the instrument texture remains PNG. The JPEGs are review evidence, not
texture assets shipped by the simulator.

| Pair | What changed | Fixed view / scene |
| --- | --- | --- |
| [Town before](town-before.jpg) / [after](town-after.jpg) | Framed and panelled doors, quieter plaster variation | `--view -78 9 -30 50 -6`, town |
| [Pedestrian before](pedestrian-before.jpg) / [after](pedestrian-after.jpg) | Rounded shared body geometry, head details, hands, varied hair | `--spawn square --view -61 6.5 14 0 -4`, two frames |
| [Fog before](fog-before.jpg) / [after](fog-after.jpg) | Distant tree batches culled at dense-fog visibility range | Same fixed town camera and fog preset |
| [Cockpit before](cockpit-before.jpg) / [after](cockpit-after.jpg) | Steering wheel position, material separation and dashboard visibility | Stationary noon cockpit at the square |
| [Cluster before](cluster-before.png) / [after](cluster-after.png) | Fewer colliding speed labels; odometer and trip fit the centre screen | Same 420 km/h instrument scale |

Additional checks: [night cockpit](cockpit-night.jpg), [walking camera](walking.jpg),
[helicopter aerial view](flight.jpg). The cluster is shown while stationary with its engine
off, so it checks layout rather than gauge motion. The night frame likewise checks cabin
visibility; live illumination under every weather combination still needs review.

These are incremental improvements. The town still needs more regional facade and street
detail, pedestrians still need finer clothing and gait work, and the cockpit needs a broader
production-quality geometry/material pass. The aerial frame is useful for spotting culling
and density problems as Phase 14 proceeds.
