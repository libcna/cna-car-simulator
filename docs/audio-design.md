# Audio design

All sounds are synthesised in project code at start-up or in real time; no recorded samples
are shipped, so there is nothing to license. Playback uses one stereo
`DynamicSoundEffectInstance` (44.1 kHz, 16-bit) from the XNA 4.0 audio API; mixing happens in
project code (`simulator/src/Audio`).

The [Phase 14 listening pack](audio-previews/phase14/README.md) now provides reproducible,
device-free renders of the actual mixer for startup, idle, RPM and load changes, lift-off,
shifts, engine braking, tyre/road, wind, rain, snow, traffic pass-by, cabin switching and
helicopter modes. The WAV outputs repeated byte-for-byte and the tracked FLAC files decode
to the same PCM. Their peaks and file integrity have been checked. A real-speaker or headphone
listening pass and any mix changes based on it are still required; these files do not by
themselves establish perceived quality.

## Stream and buffering

`Audio::VehicleAudio` keeps three 1024-frame blocks (about 70 ms) queued. Each frame it asks
the instance for `PendingBufferCount` and renders blocks until three are pending, converts to
interleaved 16-bit PCM and calls `SubmitBuffer`. `Play()` starts once the first blocks are
queued. Underruns (pending count reaching zero after start) are counted and shown in the debug
overlay (F3, "audio stream"). Audio can be disabled with `--no-audio`; a failed device also
disables it without stopping the simulator.

The web build keeps eight blocks (about 186 ms) queued instead, and asks SDL for a 2048-frame
device buffer (`SDL_AUDIO_DEVICE_SAMPLE_FRAMES`, set in `Program.cpp` before the device opens).
In the browser the audio callback runs on the main thread and pulls a large chunk at a time, while
the frame loop runs zero, one or several updates per animation frame; with three blocks the queue
ran dry during a slow frame and the stream played gaps.

## Engine (`Audio::EngineSynth`)

- Crank phase accumulator from the simulated rpm (phase continuous across blocks; rpm, load
  and throttle ramp linearly across each block so parameter changes never click).
- Harmonic bank at multiples of the crank frequency with a four-cylinder character: dominant
  second order (firing frequency = rpm / 30), even orders strong, odd orders weak; each
  harmonic keeps an "idle share" at zero load and grows with load. Harmonics above 0.45 of the
  sample rate are skipped; a 1.8 kHz low-pass tames high rpm. The upper orders now crossfade
  smoothly from half strength at low RPM to full strength at high RPM, while the lower orders
  retain body at idle.
- Exhaust pulse train: a short decaying noise burst with a resonant thump (about 95-160 Hz)
  triggered at every firing event; amplitude follows load.
- A quiet valve-train whine at order 7.5 follows rpm. The older continuous broadband intake
  hiss was removed because it made idle sound like a constant leak; the pulse train provides
  the irregular exhaust texture.
- A separate intake texture passes deterministic noise through an RPM-dependent band and
  gates it at the firing rhythm. Its gain follows the real throttle and torque input, so a
  pedal blip adds texture before delivered load rises; closed throttle and idle have no
  continuous intake hiss. Throttle, load and RPM ramp across every 1024-sample block.
- Starter: a 96 Hz whine with wobble while the engine state is `Starting`; the engine model's
  cranking rpm (about 280) drives the slow chug; a "catch" clip plays on the transition to
  `Running`.
- Off/stalled: the master gain fades out over 0.18 s.

Load is the engine model's delivered torque fraction (`VehicleState::engineLoad`, 0 on
overrun) blended with a small throttle share so a blipped pedal is audible immediately.
The F3 developer overlay shows end-of-block RPM, firing rate and the tonal, exhaust, intake
and master coefficients. These are mix coefficients, not measured sound pressure levels.
Deterministic tests compare the 0.8–2.4 kHz band with open and closed throttle at the same
RPM/load, assert bounded output and check the first sample after a pedal change for a click.

At this increment, `carsim-simtrace engine-sound` reported mono engine RMS 0.072 at idle,
0.106 at light 2200 rpm cruise, 0.272 at full 3000 rpm, 0.263 at full 5500 rpm and 0.063
on 3000 rpm overrun. The >2 kHz energy share stayed 0.2–1.3%. These are signal measurements;
they do not establish perceived quality on real speakers.
An offscreen 90-frame square-start run with the dummy stereo stream and 20 warmed traffic
cars measured 0.338 ms mean audio update (60 measured frames, after 30 warm-up frames).
The dummy stream reported no errors; this is CPU-path evidence, not a speaker check.

### Recorded-source research checkpoint

The [CC0 Mini Cooper engine contact recording](https://freesound.org/people/TheLittleCrow/sounds/669618/)
and [CC0 sedan loop](https://freesound.org/people/Dmitry_mansurev64/sounds/748027/)
have clear published redistribution rights, but Freesound requires login for their originals;
the latter is a single loop with no documented stable RPM layers. The
[public-domain Opel Corsa startup](https://commons.wikimedia.org/wiki/File:Open_Corsa_E_model_2014_engine_startup_sound.ogg)
is only four seconds and does not supply the driving layers. The
[CC BY 4.0 Beetle recording](https://commons.wikimedia.org/wiki/File:WWS_VolkswagenBeetle8211engine.ogg)
is an air-cooled flat-four with a different character. None was added to the build: stable
loop points, RPM labels and perceptual fit still need validation. Any adopted file requires
the source, license, author, hash and conversion history in `assets/manifest.json`.

### Starter-to-idle transition

The starter motor's 96 Hz tone now fades in over 25 ms while cranking and out
over 40 ms after ignition catches. Previously the motor disappeared at the
first sample of the running block, producing a deterministic 0.094 sample
step in an isolated fixed-RPM test. The new test compares the first running
sample against a continued-cranking reference and verifies the motor releases
during that block. The existing catch clip and engine-state timing remain
unchanged. This removes one transition artefact; a real-speaker review of the
whole start sequence remains necessary.

## Layers driven by the drive state (`Audio::Layers`)

Pure envelope functions in `AudioLayers.hpp`, applied per block by `VehicleAudio::RenderBlock`:

- **Gear-change dip** (`ShiftDip`): on a gear change while rolling faster than 3 km/h the
  engine load input is multiplied by 0.2 and recovers quadratically to 1.0 over 0.26 s, so a
  shift "breathes" instead of the note simply stepping.
- **Overrun burble** (`OverrunBurble`): while the wheels push the engine (load and throttle
  below 5 %, rpm above 2200, speed above 15 km/h) a hashed gate fires on roughly a third of
  the 23 ms blocks (denser at high rpm) and adds 0.10-0.22 of exhaust load for that block:
  irregular pops on the overrun, deterministic per block index.
- **Surface rolling noise** (`SurfaceRoughness`): the tyre noise amplitude is scaled by the
  average roughness of the grounded wheels' contact surfaces (asphalt 1.0, concrete 1.1,
  grass 1.4, dirt 1.6, cobbles 1.7, gravel 1.8). Surfaces come from the wheel snapshot
  (`WheelPose::surface`), so leaving the road onto a verge is audible at once. Phase 14 also
  adds a bounded granular tread layer on coarse surfaces and packed snow; the mean absolute
  grounded-wheel slip drives a brighter scrub layer. Both fade across audio blocks.
- **Brake hiss** (`BrakeHissGain`): noise low-passed at 1.4 kHz with gain
  0.16 x pedal^2 x min(speed/60, 1), silent when stopped; the gain ramps linearly across each
  block so pedal taps do not click.
- Wind stays as before (cubic growth above 60 km/h).

## Rolling noise, wind, horn, one-shots (`Audio::SoundSynth`)

- Tyres: white noise through two low-pass poles at 250 + 6 x km/h Hz (a roar, not a hiss),
  amplitude growing with speed to the power 1.5 up to its cap at 100 km/h, scaled by surface
  roughness and muted in the air. A separate low-passed granular layer responds to gravel,
  cobbles and snow; a high-passed layer follows contact-patch slip and softens on snow.
- Wind: noise high-passed at 250 Hz and low-passed twice at 1400 Hz, growing with the cube of
  speed up to its cap at 130 km/h. The same synthesizer now exposes road/contact and airflow
  outputs separately; its original combined output remains the sum of those two signals.
- `carsim-simtrace engine-sound` reports each layer's level and its share of energy above 2 and
  5 kHz. Tyres and wind at 50 km/h: RMS 0.013 with 1.5 % above 2 kHz (the engine at a light
  cruise is about 0.10). They used to reach full level by 38 and 60 km/h, at 0.077 RMS with 38 %
  above 2 kHz, which made driving through town sound like hiss.
- Horn: two tones (420 and 505 Hz) with five harmonics and a 12 ms gate.
- Clips synthesised at start-up: indicator tick and tock (relay clicks on lamp on/off edges),
  gear clunk (70 Hz thud plus click; quieter in automatic), collision impact (four strengths:
  low thud, noise burst, sheet-metal ring, chosen by closing speed and rate limited), starter
  catch.

## Cockpit versus exterior

Inside the car the mix is attenuated to 55 % and low-passed at 1.7 kHz (one-pole); the blend
follows camera switches over 0.25 s. Phase 14 now advances that blend for each audio sample,
including the independently spatialized traffic layer. Previously the camera change advanced
the whole 1024-sample block at once, causing a measured 0.0027–0.0033 first-sample jump in
a deterministic engine-plus-traffic switch test. Both directions now start at exactly the
previous mix and fade within the first block; the focused test checks the first stereo sample,
later change and bounded output. That switch fix retained the established steady-state mix.
The road/contact signal gets an additional 0.90 cabin gain before the common cabin filter;
airflow gets 0.55. Exterior gains remain 1.0 for both. This lets the cabin retain tyre
contact through the structure while reducing the outside air rush more strongly. A separate
device-free integrated test compares steady 130 km/h road and airborne cases to guard this
relationship; actual speaker balance still needs listening review.

## Nearby traffic (Phase 14)

`TrafficAudio` is a device-independent spatial layer mixed into the same XNA stereo stream.
The game supplies each active traffic vehicle's pose, velocity, speed, acceleration and heavy
vehicle flag after traffic updates. At most six vehicles within 90 m are audible at once;
another six voice slots allow departing cars to fade while new ones approach. Existing voice
IDs keep their oscillator phase across blocks. Sound frequency follows speed with a small
relative-velocity shift, buses and lorries have a lower/stronger engine, and left/right gain
follows the vehicle's direction from the listener. A 90 ms gain envelope removes entry/exit
clicks. The cabin attenuates this outdoor layer more than the player's own engine. A soft
ceiling handles unusually loud overlaps instead of hard PCM clipping. The F3 overlay reports
active traffic voices. No traffic synthesis runs when audio is disabled.

The fixed 90-frame offscreen runtime with 19–20 cars and the dummy audio device reported a
0.37 ms mean project audio update (320 × 200, OPENGLES3 llvmpipe); this is CPU mixer time, not
a real speaker listening review. The source remains fully procedural, and the wider engine,
weather and cabin sound goals in Phase 14 remain open.

## Helicopter rotor (Phase 14)

`RotorSynth` now owns the helicopter audio path as pure DSP. Entering flight mode fades the
car engine out while the rotor fades in over 0.12 s; leaving flight mode reverses that fade.
The accepted 5/6/7/8 Hz normal/turbo/ultra/ultra-ultra rotor cadence is preserved. The
existing low blade thrum remains, with a filtered noise sweep shaped at each blade passage
and a quieter continuous turbine tone. The sweep is phase-continuous and shaped rather than
a constant wideband hiss; the same cockpit low-pass and output ceiling still apply. The
new component has no audio-device or renderer dependency, and `VehicleAudio` only chooses
the current flight state and rotor speed.

Two device-free integrated tests characterize fade-out, bounded samples, and the dominant
blade cadence of normal and extreme flight modes. A 90-frame offscreen flight runtime with
the dummy audio device, 320 × 200 low graphics and 60 measured frames reported 0.332 ms
mean project audio update. This is a mixer CPU timing, not a speaker listening assessment.

## Levels

`AudioLevels`: master 0.8, engine 1.0, effects 1.0, cockpit attenuation 0.55, cockpit
low-pass 1700 Hz. Master, engine and effects levels persist in the save file.

## Tests

`tests/Audio/EngineSynthTests.cpp`: silence when off and fade-in when running; the firing
frequency dominates the spectrum at 1500/3000/4500 rpm; load increases loudness; block
boundaries are continuous; the starter-to-idle release starts continuously; clips are bounded
and short; rolling noise grows with speed.
The Phase 14 rolling test compares steady dry asphalt, gravel, packed snow and full slip,
also checking bounded level and reduced airborne tyre sound. A split-output test checks
that road plus airflow reconstructs the prior combined output sample by sample.
`tests/Audio/TrafficAudioTests.cpp` checks stereo direction, distance falloff, six-voice
prioritisation, fade-out, finite level and cabin attenuation in the integrated vehicle mixer.
`tests/Audio/VehicleAudioMixTests.cpp` checks the first sample and within-block transition
for both exterior-to-cockpit and cockpit-to-exterior switches with engine and traffic active,
and checks that the cabin muffles airflow more strongly than road contact.
`tests/Audio/VehicleAudioFlightTests.cpp` checks isolated rotor fade, bounded output, and
normal versus extreme rotor cadence through the integrated mixer without an audio device.
`tests/Audio/AudioLayersTests.cpp`: the shift dip cuts to 0.2 and recovers within 0.26 s; the
overrun gate fires only on overrun and on 15-55 % of blocks; brake hiss grows with pedal and
speed and is bounded; surface roughness ordering (gravel > grass > asphalt, cobbles > concrete).
