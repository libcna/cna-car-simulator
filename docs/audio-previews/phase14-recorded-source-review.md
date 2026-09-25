# Phase 14 recorded engine source review (2026-09-25)

The listener judged every procedural engine scene unnatural. The original startup sputtered
like an old car; revision 1 weakened the ignition, and revision 2 sounded essentially like
the original. Both revisions were rejected as perceptual fixes. The later Honda candidate
below was accepted for a recorded start and idle layer; the rest of the RPM range remains open.

## Sources checked

| Source | Licence shown by source | What it provides | Decision |
| --- | --- | --- | --- |
| [Saturn Vue start, idle, stop](https://freesound.org/people/tbsounddesigns/sounds/405322/) by tbsounddesigns | CC0 | 12.5 s stereo recording of a 2004 manual Saturn Vue | Listener preferred its public preview to the Fiat candidate. Original 44.1 kHz/24-bit WAV requires a Freesound login; the public preview is lossy MP3. |
| [Fiat Punto start, idle, stop](https://freesound.org/people/sound_catcher99/sounds/425158/) by sound_catcher99 | CC0 | 12.18 s mono recording of a Fiat Grande Punto petrol engine | Listener preferred Saturn. |
| [BMW 120d engine pack](https://freesound.org/people/GiocoSound/packs/22622/) by GiocoSound | Individual clips show CC0 | Separate interior/exterior start, idle, low, medium and high RPM, and stop | Technically useful set, but it is a diesel and has not yet had a listener verdict for the game's petrol hatchback. Originals require login. |
| [Mini Cooper S engine contact recording](https://freesound.org/people/TheLittleCrow/sounds/669618/) by TheLittleCrow | CC0 | Long continuous engine recording with RPM changes | No loop or calibrated RPM layers yet; original requires login. |
| [Opel Astra engine loop](https://opengameart.org/content/car-engine-loop-96khz-4s) by qubodup | CC BY 3.0 / GPL 2.0 / GPL 3.0 on OpenGameArt | 4 s real recording edited into one loop | Downloaded for local evaluation, but one fixed loop does not cover idle-to-redline dynamics. The asset checker does not currently accept CC BY 3.0. |
| [Car Engine Start, Idle, Revving](https://freesound.org/people/NHumphrey/sounds/200973/) by NHumphrey | CC0 | 39 s start, idle and rev recording | Listener rejected both the public preview and a gain-only +17 dB comparison; its colour, not only level, was unsuitable. |
| [Performance Cars pack](https://muted.io/performance-cars/) by muted.io | CC0 | 68 numbered 96 kHz/24-bit stereo field recordings | Original WAV 039 was auditioned at source level; listener said it sounded like a motorcycle. No pack sound was imported. |
| [Honda Engine Start and Rev.wav](https://freesound.org/people/thepodcastdoctor/sounds/487553/) by thepodcastdoctor | CC0 | 18 s recording identified by its author as a 2012 Honda Civic | Listener accepted the +4 dB public HQ preview as a suitable car character, then accepted both the uncut start-to-idle continuation and a 3.6–8.0 s idle loop with a 0.18 s overlap as natural. The source WAV needs Freesound login; the public MP3 preview is the retained evaluated source. |

The public Saturn preview was downloaded only into ignored `build/audio-candidates/` for
evaluation (SHA-256 `00c4d97a7d43fd302466970d4405103dcceb235835fcccba6ca199ca05eda070`).
At the time of the Saturn comparison none of these candidates had been imported. The
subsequently accepted Honda source and derived file are documented below.

Two local FLAC probes then used the decoded Saturn preview: a 2.85 s start crossfaded over
0.25 s into an idle excerpt, and a 3–7 s idle excerpt repeated with 0.2 s overlaps. They
are ignored build outputs, not shipped assets. The loop's boundary adjacent-sample delta
was below the global 99.9th percentile of adjacent-sample changes, but the listener rejected
both the start transition and the repeated idle: neither sounded like a car engine. A PCM
comparison found the unchanged 0–2.5 s start excerpt differs from the decoded MP3 by at most
one 16-bit sample step after FLAC conversion (correlation above 0.99999999); the 3–7 s source
window is already steady and lower in level. The failure cannot be attributed to FLAC loss.
Sample continuity alone does not establish a believable engine.
Revision 2's rejected cranking attenuation was reverted. The original procedural game audio
remained in place until the later Honda integration passed startup, idle, RPM sweep and shift
listening; the recorded layer now replaces its low-RPM character.

The source-import gate requires the original or an explicitly accepted preview, loop and RPM
validation, a reproducible conversion recipe, and an `assets/manifest.json` record with
author, source, licence and hashes. The two rejected edits remain review evidence, not
accepted audio improvements.

## Accepted Honda source and current integration boundary

The accepted preview is retained at `assets/external/audio/honda-civic-2012-start-rev-hq.mp3`
(SHA-256 `2887403c4f76019c2b426e4de199a1580fc51b4b30ffd10170fee79c64104cd1`).
The first eight seconds are decoded at the original 44.1 kHz rate, gained by 4 dB and stored
as 16-bit stereo PCM at `content/audio/honda-civic-2012-start-idle.wav` (SHA-256
`97d687819303f8a15fd6da3aa527a648d98d5dd276f8ee07c0360fc179c0988d`).
To regenerate the latter file with FFmpeg:

```sh
ffmpeg -hide_banner -loglevel error -y -i assets/external/audio/honda-civic-2012-start-rev-hq.mp3 -t 8 -af volume=4dB -ar 44100 -ac 2 -c:a pcm_s16le content/audio/honda-civic-2012-start-idle.wav
```

The initial real-time integration uses this file for recorded ignition and low-RPM idle.
It loops 3.6–8.0 s with a 0.18 s crossfade; the accepted offline probe is in the ignored
`build/audio-candidates/` directory. The listener accepted the start, idle, RPM sweep and
shifts exported from the actual mixer, but rejected fixed-RPM load, lift-off and engine
braking. Those three failures keep P14-040 open.
An isolated 0.30 s high-rev excerpt (12.72–13.02 s) with an 0.08 s overlap was also tested
as a six-second fixed-load loop; the listener heard objectionable repetition. It was kept
only as an ignored probe in `build/audio-candidates/`, and was not imported into the game.
The environmental mix was subsequently reviewed separately; its verdicts are in the
[Honda mixer pack](phase14-honda/README.md).
