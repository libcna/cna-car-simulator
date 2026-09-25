# Phase 14 Honda + Mini mixer listening review

These six 44.1 kHz stereo FLACs are the scenarios changed by the Mini Cooper S
mid/high-RPM layer in the game's `VehicleAudio` mixer. The listener accepted all six:
[load](load.flac), [lift-off](lift_off.flac), [engine braking](engine_braking.flac),
[RPM sweep](rpm_sweep.flac), [shifts](shifts.flac) and [cabin/exterior switching](cabin_switch.flac).
The other eight scenarios have PCM byte-identical to the accepted
[Honda mixer pack](../phase14-honda/README.md): startup, idle, tyre/road, wind, rain,
snow, the revised traffic pass-by and helicopter. The fixed-RPM load, lift-off and
braking scenes were the three earlier rejected scenes; the new mixer passed listener
review for each. All 14 current preview peaks are below 0.53 full scale.

The [asset manifest](../../../assets/manifest.json) identifies both CC0 recordings,
their source and derived hashes. The [source review](../phase14-recorded-source-review.md)
records the conversion command and loop selection. A focused regression checks the
load-loop boundary against ordinary adjacent-sample changes. Two device-free exports
of the changed load, lift-off and braking WAVs matched byte for byte. FLAC decoding
of all six tracked files matches the corresponding preview WAV PCM.

Regenerate with the existing build:

```sh
cmake --build build/opengl33 --target carsim-audiopreview
build/opengl33/bin/carsim-audiopreview build/audio-preview/phase14-mini-regenerated
```

Convert each changed WAV with `ffmpeg -hide_banner -loglevel error -i INPUT.wav -c:a flac OUTPUT.flac`.
The preview tool uses the game mixer without opening an audio device or graphical window.
