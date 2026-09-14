# Audio design

All sounds are synthesised in project code at start-up or in real time; no recorded samples
are shipped, so there is nothing to license. Playback uses one stereo
`DynamicSoundEffectInstance` (44.1 kHz, 16-bit) from the XNA 4.0 audio API; mixing happens in
project code (`simulator/src/Audio`).

## Stream and buffering

`Audio::VehicleAudio` keeps three 1024-frame blocks (about 70 ms) queued. Each frame it asks
the instance for `PendingBufferCount` and renders blocks until three are pending, converts to
interleaved 16-bit PCM and calls `SubmitBuffer`. `Play()` starts once the first blocks are
queued. Underruns (pending count reaching zero after start) are counted and shown in the debug
overlay. Audio can be disabled with `--no-audio`; a failed device also disables it without
stopping the simulator.

## Engine (`Audio::EngineSynth`)

- Crank phase accumulator from the simulated rpm (phase continuous across blocks; rpm, load
  and throttle ramp linearly across each block so parameter changes never click).
- Harmonic bank at multiples of the crank frequency with a four-cylinder character: dominant
  second order (firing frequency = rpm / 30), even orders strong, odd orders weak; each
  harmonic keeps an "idle share" at zero load and grows with load. Harmonics above 0.45 of the
  sample rate are skipped; a 1.8 kHz low-pass tames high rpm.
- Exhaust pulse train: a short decaying noise burst with a resonant thump (about 95-160 Hz)
  triggered at every firing event; amplitude follows load.
- Intake hiss (low-passed noise) follows the throttle; valve-train whine at order 7.5 follows
  rpm.
- Starter: a 96 Hz whine with wobble while the engine state is `Starting`; the engine model's
  cranking rpm (about 280) drives the slow chug; a "catch" clip plays on the transition to
  `Running`.
- Off/stalled: the master gain fades out over 0.18 s.

Load is the engine model's delivered torque fraction (`VehicleState::engineLoad`, 0 on
overrun) blended with a small throttle share so a blipped pedal is audible immediately.

## Rolling noise, wind, horn, one-shots (`Audio::SoundSynth`)

- Tyres: low-passed white noise, cutoff rising with speed, amplitude proportional to the square
  of speed (capped), scaled by surface roughness and muted in the air.
- Wind: band-passed noise (250-1400 Hz) with cubic speed growth above about 60 km/h.
- Horn: two tones (420 and 505 Hz) with five harmonics and a 12 ms gate.
- Clips synthesised at start-up: indicator tick and tock (relay clicks on lamp on/off edges),
  gear clunk (70 Hz thud plus click; quieter in automatic), collision impact (four strengths:
  low thud, noise burst, sheet-metal ring, chosen by closing speed and rate limited), starter
  catch.

## Cockpit versus exterior

Inside the car the mix is attenuated to 55 % and low-passed at 1.7 kHz (one-pole); the blend
follows camera switches over 0.25 s so `C` never clicks.

## Levels

`AudioLevels`: master 0.8, engine 1.0, effects 1.0, cockpit attenuation 0.55, cockpit
low-pass 1700 Hz. Settings persistence arrives with M9.

## Tests

`tests/Audio/EngineSynthTests.cpp`: silence when off and fade-in when running; the firing
frequency dominates the spectrum at 1500/3000/4500 rpm; load increases loudness; block
boundaries are continuous; clips are bounded and short; rolling noise grows with speed.
