# Recorded engine sources

The player car's engine sound mixes two recorded CC0 layers with the project's own RPM-driven
synthesis (`docs/audio-design.md`). This page records where those recordings came from, why
they were chosen over the other candidates, and how the shipped files are derived from them.
The machine-checked record is `assets/manifest.json` (licence, author, source URL and SHA-256
of every file), summarised in [`assets/ASSETS.md`](../../assets/ASSETS.md) and verified by
`scripts/check_assets.py`.

## Why recordings at all

A fully procedural engine was built first. Listening on real speakers judged every procedural
engine scene unnatural, and the start-up sputtered like a much older car. Two targeted
revisions were tried and rejected by ear: the first weakened the ignition (worse), the second
only quietened cranking combustion and still sounded like the original. Both were reverted.
The procedural synthesiser stays in the mix for dynamic RPM texture and as the fallback when a
recording is missing; the recordings supply the character it lacked.

## Sources checked (2026-09-25)

| Source | Licence shown by source | What it provides | Decision |
| --- | --- | --- | --- |
| [Saturn Vue start, idle, stop](https://freesound.org/people/tbsounddesigns/sounds/405322/) by tbsounddesigns | CC0 | 12.5 s stereo recording of a 2004 manual Saturn Vue | Preferred to the Fiat candidate as an untouched preview, but two edited start/idle loop probes made from it were rejected: neither sounded like a car engine. Original 44.1 kHz/24-bit WAV requires a Freesound login; the public preview is lossy MP3. |
| [Fiat Punto start, idle, stop](https://freesound.org/people/sound_catcher99/sounds/425158/) by sound_catcher99 | CC0 | 12.18 s mono recording of a Fiat Grande Punto petrol engine | Saturn was preferred. |
| [BMW 120d engine pack](https://freesound.org/people/GiocoSound/packs/22622/) by GiocoSound | Individual clips show CC0 | Separate interior/exterior start, idle, low, medium and high RPM, and stop | Technically useful set, but a diesel; never auditioned for the game's petrol hatchback. Originals require login. |
| [Mini Cooper S engine contact recording](https://freesound.org/people/TheLittleCrow/sounds/669618/) by TheLittleCrow | CC0 | 108 s stereo contact-microphone recording; left is chassis, right is engine block | **Adopted** for the mid/high-RPM load layer (see below). Original WAV requires login. |
| [Opel Astra engine loop](https://opengameart.org/content/car-engine-loop-96khz-4s) by qubodup | CC BY 3.0 / GPL 2.0 / GPL 3.0 on OpenGameArt | 4 s real recording edited into one loop | One fixed loop does not cover idle-to-redline dynamics, and `scripts/check_assets.py` does not accept CC BY 3.0. |
| [Car Engine Start, Idle, Revving](https://freesound.org/people/NHumphrey/sounds/200973/) by NHumphrey | CC0 | 39 s start, idle and rev recording | Rejected as it was and after a +17 dB level match; its colour, not only its level, was unsuitable. |
| [Performance Cars pack](https://muted.io/performance-cars/) by muted.io | CC0 | 68 numbered 96 kHz/24-bit stereo field recordings | Recording 039 was auditioned at source level and sounded like a motorcycle. Nothing imported. |
| [Honda Engine Start and Rev.wav](https://freesound.org/people/thepodcastdoctor/sounds/487553/) by thepodcastdoctor | CC0 | 18 s recording identified by its author as a 2012 Honda Civic | **Adopted** for start-up and low-RPM idle (see below). The source WAV needs a Freesound login; the public HQ MP3 preview is the retained, evaluated source. |

Earlier, before these hosts were reachable, a
[CC0 sedan loop](https://freesound.org/people/Dmitry_mansurev64/sounds/748027/), a
[public-domain Opel Corsa start-up](https://commons.wikimedia.org/wiki/File:Open_Corsa_E_model_2014_engine_startup_sound.ogg)
and a [CC BY 4.0 VW Beetle recording](https://commons.wikimedia.org/wiki/File:WWS_VolkswagenBeetle8211engine.ogg)
were also considered: a single loop without RPM layers, four seconds only, and an air-cooled
flat-four respectively. None was used.

A lesson from the Saturn probes: sample continuity at a loop point does not make a believable
engine. The probe's boundary step was below the 99.9th percentile of ordinary adjacent-sample
changes and its FLAC round trip was within one 16-bit step of the decoded MP3, yet the listener
still rejected it. Loop maths is a necessary check, not an acceptance criterion.

## Import gate

A recorded source is imported only with: the original or an explicitly accepted preview; loop
and RPM validation by listening through the actual game mixer; a reproducible conversion
recipe; and an `assets/manifest.json` record with author, source, licence and hashes. Local
listening probes and downloaded candidates live in the ignored `build/audio-candidates/`
directory and are never committed.

## Honda Civic: start-up and idle

The accepted preview is retained at `assets/external/audio/honda-civic-2012-start-rev-hq.mp3`
(SHA-256 `2887403c4f76019c2b426e4de199a1580fc51b4b30ffd10170fee79c64104cd1`).
The first eight seconds are decoded at the original 44.1 kHz rate, gained by 4 dB and stored
as 16-bit stereo PCM at `content/audio/honda-civic-2012-start-idle.wav` (SHA-256
`97d687819303f8a15fd6da3aa527a648d98d5dd276f8ee07c0360fc179c0988d`):

```sh
ffmpeg -hide_banner -loglevel error -y -i assets/external/audio/honda-civic-2012-start-rev-hq.mp3 -t 8 -af volume=4dB -ar 44100 -ac 2 -c:a pcm_s16le content/audio/honda-civic-2012-start-idle.wav
```

The mixer plays the whole start sequence on ignition and then loops 3.6–8.0 s with a 0.18 s
crossfade. The listener accepted the +4 dB character, the uncut start-to-idle continuation
and the loop. With the Honda layer alone, the actual-mixer start, idle, RPM sweep and shifts
were accepted but fixed-RPM load, lift-off and engine braking were not; a 0.30 s high-rev
excerpt (12.72–13.02 s) looped for six seconds was rejected for audible repetition and never
imported. That gap is what the Mini layer fills.

## Mini Cooper S: mid/high-RPM load

The [source page](https://freesound.org/people/TheLittleCrow/sounds/669618/) identifies the car,
the two microphone placements and CC0 terms. The retained public HQ preview is
`assets/external/audio/mini-cooper-s-contact-hq.mp3` (SHA-256
`5f487dd82d5c4a75e44e651dcaa9c48617d36c0e475cb167987ebc57dba9f07d`).
The listener chose its 65–81 s excerpt over another CC0 rev recording, then preferred a steady
29.0–33.0 s window over a louder 77.2–79.6 s loop in a 19 s repeated-loop comparison. The
derived 44.1 kHz stereo PCM16 asset is `content/audio/mini-cooper-s-load.wav` (SHA-256
`5576a2d8fe9e47cab84c89cc0fb636963b3580cc63f7d2066373958a9ed46ae6`); it mixes chassis and
engine-block channels 20/80:

```sh
ffmpeg -hide_banner -loglevel error -y -ss 29 -i assets/external/audio/mini-cooper-s-contact-hq.mp3 -t 4 -af 'pan=stereo|c0=0.2*c0+0.8*c1|c1=0.2*c0+0.8*c1,volume=10dB' -ar 44100 -c:a pcm_s16le content/audio/mini-cooper-s-load.wav
```

At runtime the last 0.35 s overlaps the first 0.35 s, and playback rate follows RPM within
0.75–1.35×. A regression test caught an initial wrong overlap index (boundary jump 0.232
against a 0.043 99.9th-percentile ordinary sample change) and now guards the corrected loop.

## Listening review of the shipped mix

Every verdict below was given on real speakers or headphones against device-free renders of
the actual `VehicleAudio` mixer. With both recordings installed:

- **Accepted:** start-up, idle, RPM sweep, shifts, fixed-RPM load, lift-off, engine braking,
  cabin/exterior switching, tyre/road, wind, rain with wipers, snow and the helicopter rotor.
- **Traffic pass-by:** the first render was inaudible (−35.7 dBFS mean); after a seeded tyre/air
  layer and a stronger nearby voice it measures −29.2 dBFS mean / −13.7 dBFS peak and was
  accepted as audible and natural.

The renders are not kept in the repository. To hear them again, build the `carsim-audiopreview`
tool (`tools/audio_preview/`) and write the fourteen WAV scenarios into the ignored build tree;
it opens no audio device and no window, and two exports are byte-identical:

```sh
cmake --build --preset opengles3 --target carsim-audiopreview
build/opengles3/bin/carsim-audiopreview build/audio-preview
```
