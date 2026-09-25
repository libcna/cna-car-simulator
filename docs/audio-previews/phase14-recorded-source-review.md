# Phase 14 recorded engine source review (2026-09-25)

The listener judged every procedural engine scene unnatural. The original startup sputtered
like an old car; revision 1 weakened the ignition, and revision 2 sounded essentially like
the original. Both revisions were rejected as perceptual fixes. No recorded engine sound is
used by the game yet.

## Sources checked

| Source | Licence shown by source | What it provides | Decision |
| --- | --- | --- | --- |
| [Saturn Vue start, idle, stop](https://freesound.org/people/tbsounddesigns/sounds/405322/) by tbsounddesigns | CC0 | 12.5 s stereo recording of a 2004 manual Saturn Vue | Listener preferred its public preview to the Fiat candidate. Original 44.1 kHz/24-bit WAV requires a Freesound login; the public preview is lossy MP3. |
| [Fiat Punto start, idle, stop](https://freesound.org/people/sound_catcher99/sounds/425158/) by sound_catcher99 | CC0 | 12.18 s mono recording of a Fiat Grande Punto petrol engine | Listener preferred Saturn. |
| [BMW 120d engine pack](https://freesound.org/people/GiocoSound/packs/22622/) by GiocoSound | Individual clips show CC0 | Separate interior/exterior start, idle, low, medium and high RPM, and stop | Technically useful set, but it is a diesel and has not yet had a listener verdict for the game's petrol hatchback. Originals require login. |
| [Mini Cooper S engine contact recording](https://freesound.org/people/TheLittleCrow/sounds/669618/) by TheLittleCrow | CC0 | Long continuous engine recording with RPM changes | No loop or calibrated RPM layers yet; original requires login. |
| [Opel Astra engine loop](https://opengameart.org/content/car-engine-loop-96khz-4s) by qubodup | CC BY 3.0 / GPL 2.0 / GPL 3.0 on OpenGameArt | 4 s real recording edited into one loop | Downloaded for local evaluation, but one fixed loop does not cover idle-to-redline dynamics. The asset checker does not currently accept CC BY 3.0. |

The public Saturn preview was downloaded only into ignored `build/audio-candidates/` for
evaluation (SHA-256 `00c4d97a7d43fd302466970d4405103dcceb235835fcccba6ca199ca05eda070`).
No file from these sources has been imported into `content/` or `assets/`.

Two local FLAC probes then used the decoded Saturn preview: a 2.85 s start crossfaded over
0.25 s into an idle excerpt, and a 3–7 s idle excerpt repeated with 0.2 s overlaps. They
are ignored build outputs, not shipped assets. The loop's boundary adjacent-sample delta
was below the global 99.9th percentile of adjacent-sample changes, but the listener rejected
both the start transition and the repeated idle: neither sounded like a car engine. A PCM
comparison found the unchanged 0–2.5 s start excerpt differs from the decoded MP3 by at most
one 16-bit sample step after FLAC conversion (correlation above 0.99999999); the 3–7 s source
window is already steady and lower in level. The failure cannot be attributed to FLAC loss.
Sample continuity alone does not establish a believable engine.
Revision 2's rejected cranking attenuation was reverted. Keep the original procedural game audio until
a complete recorded implementation passes listening in startup, idle, RPM sweep and shifts.

Any future import needs the original or an explicitly accepted preview, loop and RPM
validation, a reproducible conversion recipe, and an `assets/manifest.json` record with
author, source, licence and hashes. The existing two rejected edits are kept as review
evidence; they are not treated as accepted audio improvements.
