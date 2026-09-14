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

## Rear-view mirror

The interior mirror renders a 768 x 200 target from `mirrorCenter` looking back along the body
(11 degree vertical field, about 40 degrees horizontal), mirrored in x. The setting
`mirrorUpdateEvery` in the save file (or `--mirror-every <n>`) redraws it every n frames and
keeps the previous image in between; see `docs/performance.md` for the measured cost.

## Inspection modes

- `--chase-yaw <deg> --chase-distance <m>` orbit the chase camera around the car.
- `--view x y z heading pitch` places a fixed free camera (metres, degrees, heading 0 = north).
- `--lockstep` runs one simulation step per drawn frame so captures on slow renderers are
  deterministic; `--auto-drive <s>` scripts a start and a gentle drive.
