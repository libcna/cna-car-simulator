# Phase 14 engine revision 1: listening comparison

**Rejected after listening:** the listener judged the revised startup worse than the original.
The simulator's engine mix has been restored to the original version. These files remain only
as comparison evidence; they are not the current game sound.

Listener feedback on the [original pack](../phase14/README.md) said all engine clips sounded
unnatural and the startup sputter evoked a much older car. These seven clips use the same
deterministic scenarios and actual `VehicleAudio` mixer after a first targeted adjustment.
They are 44.1 kHz, 16-bit stereo FLAC, lossless from the exported PCM. No recording was added.

| Scenario | Original | Revision 1 |
| --- | --- | --- |
| Startup: cranking, catch, idle | [original](../phase14/startup.flac) | [revised](startup.flac) |
| Idle | [original](../phase14/idle.flac) | [revised](idle.flac) |
| RPM sweep | [original](../phase14/rpm_sweep.flac) | [revised](rpm_sweep.flac) |
| Fixed-RPM load | [original](../phase14/load.flac) | [revised](load.flac) |
| Lift-off | [original](../phase14/lift_off.flac) | [revised](lift_off.flac) |
| Shifts | [original](../phase14/shifts.flac) | [revised](shifts.flac) |
| Engine braking | [original](../phase14/engine_braking.flac) | [revised](engine_braking.flac) |

The cranking motor now runs without low-RPM combustion pulses. The running engine fades in
over 35 ms when it catches; the catch transient is shorter and quieter. Exhaust pulses sit
lower under the tonal engine, and overrun burble is less prominent. Shift timing and the
 scenario controls are unchanged. A technical check found no clipped samples (maximum peak
0.597 of full scale); this does not establish that the new sound is good. The startup
comparison was rejected by ear; the other six clips have no acceptance judgment.
Two fresh WAV exports of these seven scenarios were byte-identical, and all seven FLAC files
decoded to the exact exported PCM.

Reproduce the WAVs with `carsim-audiopreview build/audio-preview/phase14-revision-1` from
the repository root. `SHA256SUMS` records the tracked FLAC files.
