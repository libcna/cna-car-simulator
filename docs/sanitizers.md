# Project sanitizer build

The `asan-ubsan` preset instruments **simulator-owned** targets with GCC/Clang
AddressSanitizer and UndefinedBehaviorSanitizer, keeps frame pointers, and stops on a reported
undefined operation. CNA and Sharp Runtime are consumed as separate upstream checkouts and
are not instrumented by this project option.
The simulator targets use `-O1 -g` in this preset so the long deterministic traffic tests
finish in a practical time while preserving useful symbols in reports. The dedicated core
registration has a 30-minute wall-clock timeout.

```bash
cmake --preset asan-ubsan
cmake --build build/asan-ubsan --target carsim_core_tests -j2
ctest --preset asan-ubsan -R carsim_core_sanitizer_tests --output-on-failure
```

If a managed shell makes the shared compiler cache read-only, set
`CCACHE_DIR=/tmp/carsim-ccache` for configure and build. A normal developer checkout does not
need that override.

The core-only target links no graphics/audio device library. It includes vehicle, boost,
helicopter, map, collision, walking, traffic, overtaking and the simulated 30-minute traffic
soak, plus pure engine/road/traffic audio DSP tests. Integrated mixer and renderer tests remain
in `carsim_tests`; use the normal `opengles3` preset for the full six-registration suite.

LeakSanitizer needs permission to inspect threads at process exit. The managed restricted
shell used for the Phase 14 development run returned `LeakSanitizer has encountered a fatal
error` even for two passing targeted tests. Running the same instrumented binary in the
normal desktop process environment completed with leak detection **enabled**. Do not disable
leak checks to turn that environment error into a pass.

The first core run exposed a real `stack-use-after-scope`: a helicopter collision test passed
a temporary `VehicleDefinition` to `Vehicle`, which stored a reference and then read it after
the temporary expired. `Vehicle` now owns an immutable copy. Its engine, driveline, tyre and
wheel references point into that owned definition. The two affected collision cases passed
under ASan/UBSan with leak detection after the change.

The first complete rerun then reached all 213 tests with no sanitizer report, but its 900 s
timeout stopped the third long soak. With `-O1`, it finished in 471 s; 212 tests passed, and
the one failure was the normal `< 6 s` map load budget (instrumented load: 10.47 s). That
budget remains six seconds in normal builds; the sanitizer core test retains a 30 s upper
bound so an actually stalled loader still fails.

The final `ctest --preset asan-ubsan -R carsim_core_sanitizer_tests --output-on-failure`
run passed **all 213 tests** in **640.35 s** in the desktop process environment, with leak
detection enabled and no ASan/UBSan finding. The three traffic soaks all completed. The
separate normal `ctest --preset opengles3` run also passed all six registrations after the
fix, including the original six-second map load assertion.
