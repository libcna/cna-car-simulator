# Vehicle physics model

Implemented in `simulator/src/Sim/`. Deterministic, single-threaded, fixed 120 Hz sub-steps
(`kPhysicsStepSeconds`), semi-implicit Euler for the body, an implicit contact-speed update for
the wheels. Every parameter comes from `VehicleDefinition` (JSON, `content/vehicles/*.json`).

## Frames and units

Right-handed XNA frame, +Y up, metres/seconds/kilograms/radians. Vehicle frame: +X right,
+Y up, -Z forward. The `RigidBody` lives at the centre of mass; wheel positions in the definition
are relative to the *vehicle origin* (centre of the wheelbase on the ground plane at rest) and are
converted to body-frame mounts at construction. Positive steering means a right turn.

## Rigid body (`RigidBody`)

6 degrees of freedom; diagonal body-frame inertia tensor rotated to world space per step;
quaternion integration with the Hamilton product (`q' = q + 0.5 dt (omega (x) q)`), normalised
each step. Gyroscopic terms are omitted (negligible for a car). Forces accumulate at world points
and produce torques about the centre of mass, so load transfer, pitch under acceleration and roll
in corners emerge from the wheel forces without special-casing.

## Suspension (`Vehicle::UpdateSuspension`)

Each wheel casts a ray from its spring mount along body-down. Compression
`c = restLength + radius - hitDistance`, clamped to `[0, travel]`. Force along body-up:
`k c + damper(c') [+ anti-roll] [+ bump stop]`, applied at the contact point. Static compression
is computed from the centre-of-mass position so the vehicle sits at its designed ride height. The
compression damper differs from the rebound damper; an anti-roll term couples the two wheels of an
axle; the last 15 % of travel adds a stiff bump-stop spring. Wheels off the ground carry no load.

## Tyres (`TyreModel`)

Simplified magic formula in normalised combined slip: with `kappa` the slip ratio and `alpha` the
slip angle, `rho = sqrt((kappa/kappaPeak)^2 + (alpha/alphaPeak)^2)`; the total force is
`Fz * mu(Fz) * shape(rho)` and is split along the two slip directions, so the friction circle is
respected automatically. `shape` is the Pacejka sine/arctangent shape with `B` solved so the peak
sits at `rho = 1`, `C` the shape factor and `E` the curvature factor. `mu` falls with load
(`loadSensitivity`) and scales with the surface (`SurfaceFrictionFactor`). Rolling resistance is a
separate longitudinal force proportional to load and the surface's rolling factor.

## Wheel spin and contact stability (`Vehicle::IntegrateWheel`)

Naively integrating wheel spin against a stiff tyre at low speed explodes at any usable time step
(the slip gain is `1/v`). The wheel therefore integrates the *relative* contact speed
`u = omega r - v` implicitly:

```
du/dt = r T / I - F(u) * (r^2/I + 1/M_share)
u_new = u_old + dt (r T / I - F0 c) / (1 + dt D c),   c = r^2/I + 1/M_share
```

where `F0` is the tyre force at the current slip and `D` the *secant* longitudinal stiffness
(always positive). The linearised force `F0 + D (u_new - u_old)`, clamped to the friction limit,
is then applied consistently to both the wheel (`omega' = omega + dt (T - r F)/I`) and the body.
This transmits drive torque as traction at standstill, lets undriven wheels follow the road speed
exactly, and remains stable on slopes and during launches. Brakes are static-friction clamps (a
brake stops a wheel but never reverses it) with a simple ABS that holds braking slip near the
tyre's peak above 1.5 m/s. Lateral forces are limited so they never reverse the contact patch's
lateral velocity within one step (the body-only equivalent of the same implicit idea), which
removes the standstill chatter classic tyre models suffer from.

## Driveline (`Vehicle::ResolveDriveline`)

Engine -> clutch/coupling -> gearbox ratio -> final drive -> differential -> driven wheels. The
differential is open by default (equal torque to each driven wheel). With `differential.type`
`"lsd"` in the vehicle JSON, or `U` in the game, it is a clutch-pack limited-slip unit
(`Vehicle::DriveWheels`): torque moves from the faster to the slower wheel, up to
`preload + powerLock * |drive torque|` (`coastLock` on the overrun), viscous inside that limit.
Two regimes for the coupling:

- **Slipping**: the coupling transmits `clamp(k_visc * slip, +-capacity)` with a narrow viscous
  band for convergence; the engine integrates freely against that load and the wheels receive the
  transmitted torque times the ratio. The pair locks when the slip falls inside 12 rad/s (or
  changes sign) with torque to spare.
- **Locked**: the engine speed is the wheel speed times the ratio; the engine's net torque and its
  reflected inertia (`I_e * ratio^2`) join the driven wheels' integration. If the torque the
  clutch would have to carry exceeds its capacity, the pair unlocks.

Manual capacity comes from the pedal through `Clutch` (smoothstep engagement band). The automatic
uses a converter-like coupling: creep torque at idle rising to full capacity `lockupSlipRpm` above
idle, never locking below idle. Below idle the creep load falls with the square of engine speed, as
a converter's absorption does, so a car held on the brake in D idles slightly low instead of
stalling. While it slips and drives, the converter multiplies the torque it
passes: `stallTorqueRatio` with the output held, falling linearly to 1:1 at a speed ratio of 0.85
(the coupling point). Multiplication times speed ratio never exceeds 0.85, so the converter trades
slip for torque without adding energy; on overrun it couples 1:1. During an automatic gear change
the controller withdraws engine torque, so a full-throttle upshift does not flare the unloaded
engine into the limiter. A manual clutch locked below the stall speed stalls the engine, which is
the realistic outcome of dumping the clutch.

The keyboard clutch is a *driver's foot*, not a switch: pressing is fast, releasing is fast down to
the bite point, then the pedal is eased through the band and never asked to carry more torque than
the engine can deliver at the current throttle (it holds at the bite point until the speeds match,
trickling only while the engine has revs in hand). Forcing the pedal (tests, scripted drives)
bypasses this and stalls the engine exactly as a real dump would.

## Engine (`Engine`)

States `Off -> Starting -> Running -> Stalled`. The starter is a speed-limited motor cranking to
`crankRpm`; after `crankSeconds` with fuel the engine catches at `catchRpm` and a decaying idle
flare. The published torque curve is net (brake) torque; closed-throttle friction/pumping losses
(`a + b rpm + c rpm^2`) act as engine braking and fade out as the throttle opens. An idle
controller opens the throttle up to 35 % below the idle target; a soft limiter fades combustion
over the last 200 rpm before `limiterRpm`. Fuel cut on overrun. Load fraction and brake power feed
the fuel and thermal models.

## Fuel (`FuelSystem`), thermal (`EngineThermal`), odometer (`Odometer`)

Fuel mass flow = idle flow scaled with rpm + BSFC(load) x brake power; the reserve lamp lights
below `reserveLiters`; when the level reaches `reserveLiters * refillAtReserveFraction` the tank
refills to `tankLiters * refillToFraction` (all configurable; the required default is 50 % of the
reserve and a full tank). Coolant temperature is a lumped mass heated by a fraction of the fuel
power and cooled through a thermostat-gated radiator, ram air and a fan, cooling slowly when off.
The odometer integrates ground speed, not wheel rotation, so wheelspin does not add kilometres.

## Steering

Road-wheel angle follows the input at a limited rate with a speed-dependent authority
(`highSpeedFactor` at `highSpeedKmh`) so keyboard steering stays controllable at speed; the
inner wheel turns more than the outer wheel (`ackermannFactor`). The steering-wheel angle shown in
the cockpit is the road-wheel angle times `steeringRatio`.

## Verified behaviour (tests/Sim/VehicleDriveTests.cpp and tools/simtrace)

Reference vehicle "Lipan 1.2" (1120 kg, 122 Nm, 5-speed): settles at ride height without drift;
manual launch at 45 % throttle moves off without stalling and locks the clutch at ~9 km/h;
dumping the clutch at idle stalls; automatic 0-100 km/h in ~14 s; 100-0 km/h in ~37 m with ABS
and no pull; straight-line stability at 90 km/h; steering right turns right with mild body roll;
handbrake holds a 12 % grade and the car rolls back when released; identical runs are
bit-identical.

`carsim-simtrace metrics --vehicle content/vehicles/lipan_12.json` measures what a keyboard driver
feels (all-or-nothing pedals and steering, the automatic choosing its gears) on the content file
itself. Current values: 0-50 km/h 5.0 s, 0-100 km/h 14.3 s, upshifts at 6,000 rpm with no flare
while shifting; 100-0 km/h in 36.8 m (mean 1.02 g), 7 m/s^2 reached 0.07 s after the key goes down;
a full keyboard steer reaches 0.5 g lateral in 0.10 s at 50 km/h and holds 0.84 g on a 19 m radius,
0.85 g at 90 km/h with 0.7 deg of body slip, and stays stable at 120 km/h (2.2 deg body slip).

## Compromises

No camber, toe or caster effects; independent suspension only (no axle kinematics); no tyre
temperature or wear; the limited-slip differential has no ramp-angle asymmetry beyond the separate
power and coast lock factors; the converter's torque
multiplication is a linear ramp rather than a measured K-factor map; aerodynamic lift and side
wind are ignored; the collision response is handled by
the collision module (impulses on the same rigid body).
