# Cameras

Two driving cameras plus two inspection modes for captures. All poses are built in
`Render/Camera.cpp` from the vehicle state; the game only picks which one to render.

## Chase camera (exterior)

`ChaseCamera` follows the car from behind:

- **Position**: an orbit point `distance + speedPull` metres behind the car (6.2 m at rest, up
  to 7.4 m at 130 km/h) and `height + speedRise` above the origin (2.0 m, up to 2.35 m),
  approached with an exponential rate of 9/s. The rate is applied as `1 - exp(-dt * 9)`, so
  the motion is the same at 30 and 60 frames per second (a test checks this). A jump of more
  than 20 m (vehicle reset) snaps instead of flying in.
- **Yaw**: the orbit direction follows the body yaw with a rate of 2.5/s when crawling and
  5.5/s above 30 km/h, so parking manoeuvres and reversing do not swing the view around; the
  camera stays behind the nose when reversing.
- **Look-ahead**: the aim point (0.9 m above the origin, 1 m ahead) slides towards the inside
  of a bend by up to 1.6 m, driven by the filtered body yaw rate and scaled by speed, so the
  road ahead stays in view in corners.
- **Ground clearance**: after smoothing, the eye is clamped to at least 0.7 m above the
  sampled ground height under it, so slopes, kerbs and road embankments never swallow the
  camera. The clamp uses the same map ground query the physics uses.
- Field of view 60 degrees, near plane 0.3 m.
- Convention: the body yaw is `atan2(-forward.x, -forward.z)` (0 facing -Z, positive turning
  left, the same as `Matrix::CreateRotationY`), so behind is `(sin yaw, 0, cos yaw)` and the
  right-hand side `(cos yaw, 0, -sin yaw)`. A test checks that the camera sits `distance`
  metres behind and aims ahead for seven headings; an earlier version had the mirror image
  and put the camera in front of an east-bound car.

## Cockpit camera

`CockpitCamera` sits at the definition's `driverEye` (Lipan: 0.37 m left of the centre line,
1.14 m up, 0.38 m behind the origin, level with the B-pillar and 0.40 m behind the windshield
header), looks straight ahead with a 64 degree vertical field of view and a 0.12 m near plane.
A small lateral offset (up to 3 cm) follows lateral acceleration with a 6/s filter as a motion
cue. `--eye dx dy dz yaw pitch` offsets and turns the eye for inspection captures.
The Lipan's steering wheel centre is 0.70 m high and 0.23 m ahead of the origin; the
instrument face is 0.99 m high and 0.39 m ahead, just in front of the dashboard fascia.
That placement keeps the entire dial face visible from the normal eye position.

## Rear-view mirror

The interior mirror renders a 768 x 200 target from `mirrorCenter` (Lipan: on the centre line,
1.28 m up, 0.12 m ahead of the origin, so its housing hangs in the upper right of the
windscreen and not across the driver's view of the road) looking back along the body
(11 degree vertical field, about 40 degrees horizontal), mirrored in x. Its far plane is 320 m
and the world pass into it is capped at 300 m (`MirrorView::kDrawDistanceM`): in a strip 200
pixels tall at eleven degrees nothing beyond that can be made out, and drawing it cost a quarter
of the cockpit frame. The setting `mirrorUpdateEvery` in the save file (or `--mirror-every <n>`,
or the graphics tier) redraws it every n frames and keeps the previous image in between; see
`docs/performance.md` for the measured cost before and after.

The **door mirrors** are convex 5 × 4 patches with a 14 mm bulge, aimed 0.20 rad outboard.
Each now has a 256 × 160 rearward render target. The game updates one wing target per frame
after both have been initialised and retains the previous image for the other side. Their
world pass is capped at 150 m; the low quality tier skips them. The earlier sky-cube-only
face read as a blank card. Phase 14's hidden Radeon isolation of rear and wing passes is in
`docs/performance.md`.

## Inspection modes

- `--chase-yaw <deg> --chase-distance <m>` orbit the chase camera around the car.
- `--view x y z heading pitch` places a fixed free camera (metres, degrees, heading 0 = north).
- `--lockstep` runs one simulation step per drawn frame so captures on slow renderers are
  deterministic; `--auto-drive <s>` scripts a start and a gentle drive.


## Measured stability

`ChaseCamera.BothCamerasStaySmoothAlongAWholeRoute` drives the sample map's `town` route with the
autopilot and measures the frame-to-frame change in the *change* of each camera's position
relative to the car -- its jerk, which is what a shimmer is made of. This build:

| camera | mean jerk | worst single frame |
| --- | ---: | ---: |
| chase | 0.044 mm/frame² | 1.7 mm |
| cockpit | 0.042 mm/frame² | 0.6 mm |

Both are well below anything visible at 60 Hz. The test's bounds sit at roughly four times these
numbers, so measurement noise will not trip them and a change that makes either camera twitchier
will. Note what this does *not* excuse: camera smoothing is not allowed to paper over unstable
physics, so the vehicle's own behaviour is measured separately in
`tests/Sim/VehicleDriveTests.cpp`.
