# Phase 14 startup revision 2: listening comparison

The first listener said the original engine family sounded unnatural and the startup
sputtered. [Revision 1](../phase14-revision-1/README.md) removed too much of the ignition
and was judged worse. This second attempt changes only the cranking phase:
combustion is 15% of its previous level while the starter turns, then reaches full level
within 10 ms of `Running`. The original catch clip, its gain and all steady engine layers
remain in place. No recording was added.

Listen to the same fixed scenario: [original startup](../phase14/startup.flac),
[rejected revision 1](../phase14-revision-1/startup.flac), and
[revision 2](startup.flac). The listener said revision 2 still sounded like the unsatisfactory
original, so it is rejected as a perceptual fix; see the
[recorded-source review](../phase14-recorded-source-review.md).

All 13 non-startup scenarios export bit-identical PCM to the original pack. In the 0.5–1.2 s
cranking window, RMS changes from 0.0752 to 0.0588 of full scale. The 1.25–1.45 s catch
window stays near its original level (0.0803 to 0.0777 RMS), and the overall startup peak
remains 0.279. These are technical checks, not a quality verdict. Two WAV exports were
byte-identical and the tracked FLAC decodes to their exact PCM. Regenerate with
`carsim-audiopreview build/audio-preview/phase14-revision-2` from the repository root.
