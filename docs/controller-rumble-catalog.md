# Controller rumble catalog and local backtesting

This tool derives a local catalog from the player's own Breath of the Wild 1.9.0 files. It does not contain game effect strings, game machine code, or generated waveforms in the repository.

## Generate and use

From this project directory:

```powershell
tools/venv/Scripts/python.exe tools/scan_rumble_resources.py
tools/venv/Scripts/python.exe tools/export_rumble_catalog.py
tools/venv/Scripts/python.exe tools/rumble_catalog_player.py
```

The scanner needs `oead` and `evfl` in the analysis Python environment. The exporter uses the existing `nso.py` and Unicorn. The player needs tkinter, the custom SDL3 DLL and libusb already deployed in `build/`.

Outputs are in ignored `build/rumble-catalog/`:

- `catalog.md`: definition table, call-site context and event references.
- `catalog.json`: source identity, six native presets and resource parameter variants.
- `resources.json`: archive coverage, duplicates, AI definitions and flow/timeline calls.
- `backtest-results.jsonl`: selected effects and software timing (older records may retain user feedback).

Do not stage these files. `Rumble-Catalog.cmd` in the local build directory starts the GUI. Search by name, source or ID, select an effect, inspect its curve and source, then press Play. Escape, Stop, focus loss and closing request silence. The player never starts vibration just because a row is selected.

The GUI uses Chinese names and explanatory text. Verified native call contexts
label small/middle damage, reverse cuts, large damage and perfect-guard hits;
these are shared preset uses, not unique effect names. Story variants retain
their exact resource/event identifiers when the scene's human-readable identity
has not been verified. Chinese scene terms are searchable.

The window fits the desktop work area, the effect list expands vertically, and
both the horizontal split and the plot/details split can be dragged. At 200%
DPI, resizing from 1560x1040 to 1900x1300 was checked with approximately 10 to 15
visible rows and accessible playback controls. Initial sash placement waits for
actual geometry to avoid collapsing the left pane before the window is mapped.

## Verified scope

The current main-module native controller preset table has six entries: three one-cycle sizes and three patterns used by duration-based requests. The event action definitions and native dispatch agree on those families. Export executes the original ARM64 addon update in Unicorn and intercepts the final HID value, avoiding a handwritten approximation of its envelope. The default constructor scale, isolated initial state and a nominal 120 Hz timebase are recorded explicitly.

The first complete local scan covered 8,363 source files, 8,655 archives/subarchives and 720 unique AI programs with no reported parser errors. It found 36 controller behavior/action definitions and 556 flow/timeline controller call records, including duplicate packaging. These yield 70 distinct repeat/duration combinations in addition to the six base presets. Current extracted RomFS coverage is not a claim to cover absent DLC content or every reachable runtime event.

The earlier captured weapon-swing amplitudes are all represented by the native exported envelopes. This validates their discrete values for those captures; it does not validate every game context or actual device output timing.

## Limits that remain visible

- Static definitions are not the same as every possible game-state combination. Distance, runtime configuration, repeated interruption, priority suppression and concurrent sources can change what actually plays.
- Resource strings and nearby animation names are provenance/context hints, not automatically reliable semantic labels for an effect.
- Duration-based variants are standalone fixtures using their resource parameters; replay does not reproduce the entire cutscene's scheduling, forks or other actors.
- Camera shake, XLink contact types and authored Betop enhancements are not native controller effect definitions and are excluded.
- Samples are source requests. The host scheduler, SDL write cadence and USB transport can coalesce or delay them; API acceptance is not a physical measurement.
- Switch 2 output still uses the experimental patent frequency mapping. No claim of calibrated HD1-to-HD2 perceptual equivalence is made.

The workspace workflow and IDA repair notes are in the [Switch research workflow](../../docs/switch-research-workflow.md) outside this repository.

The device dropdown lists SDL-recognized gamepads with name, VID/PID and instance ID. Initial selection prefers Switch 2 Pro; Refresh preserves a connected selection. Refresh and device changes stop playback, and reconnecting after an empty startup or disconnect does not require restarting the tool. Explicitly select a device after reconnecting and press Play. Enumeration does not establish support for frequency-aware rumble on every controller. Playback records include the selected device identity.

Device selection was checked on Windows using fake SDL enumeration/output (empty startup, refresh, two devices, routing, disconnect and cleanup), plus the real Tk layout at desktop DPI. These checks do not constitute physical-controller acceptance.

## Experimental amplitude comparison (2026-11-05)

### Game integration: strength ownership

The game now exposes Rumble strength next to Controller rumble in both the controller-friendly options page and the PC settings menu, with Simplified/Traditional Chinese labels. It ranges from 0 to 100 in steps of 5, defaults/resets to 50, and persists as rumble_strength in savagewind.ini. Existing files without the key use 50; out-of-range loaded integers clamp to 0..100. An existing disabled rumble toggle stays disabled.

Responsibilities: settings/UI own the percentage; runtime/rumble_strength.h owns application strength scaling and shared per-side headroom; vibration_update applies this only to its output copy after reading the game's original values. SDL owns device selection, frequency/amplitude encoding, USB scheduling and device-specific response. No Switch 2 encoding or patent formula is duplicated in the game. Raw tracing and HID readback remain unchanged. The test player's base-gain convention is the same: 10% = original, 50% = x5, 100% = x10 before saturation. This does not claim perceptual equivalence between controllers.

The game enables the experimental Switch 2 frequency path by default (explicit SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD environment opt-out is retained) and overrides the old SDL HD_GAIN to unity to avoid double amplification. Uncalibrated nonlinear test curves remain in the laboratory, not in normal gameplay. The installed custom SDL DLL already implements this path and matches the local release build hash.

0% silences ordinary vibration and gates Betop semantic companion effects; positive strength scales the ordinary low/high vibration bands, not Betop trigger resistance or binary shoulder actuators. Frequency, timing and original HID values are preserved. Saturation can flatten strong peaks, while maintaining the ratio of both bands on each side.

Validation: make test-vibration covers immediate preference changes, mute, unchanged HID readback, stereo/frequency routing, leases, focus/disconnect and shared saturation. build/test_rumble_settings checks missing-key default, saved 0/100 across processes, range clamping and preservation of a disabled rumble toggle using a separate test INI. The canonical Windows executable was rebuilt with RELEASE=1 and its GUI subsystem verified. Actual Windows/Vulkan menu appearance and controller/gameplay acceptance remain manual; the game was not automatically launched.

The user-strength slider covers 0–100%, defaults (and resets) to 50%, and uses a fixed base gain of 10. Thus 0% is silence, 10% reproduces original amplitudes before device tuning, and 100% requests tenfold amplitudes. Apply base_gain * percentage / 100 before the selected device curve. Full-scale saturation reduces both bands of a side together to preserve their ratio; saturated peaks lose amplitude contrast. Changing strength stops playback and updates the graph without automatically vibrating. Start records include strength_percent and base_strength_gain (older records used an unscaled percentage). The linear profile performs no additional enhancement. The game and SDL defaults are unchanged.

The amplitude selector defaults to the linear device curve after user strength scaling. Three optional, uncalibrated curves are restricted to Switch 2 Pro (057e:2069). Select a curve, then explicitly press Play. Changing the curve requests Stop. Original catalog samples remain unchanged; the graph overlays the output envelope and displays both peaks. Start records include the profile and curve parameter.

For each side, let m=max(low amplitude, high amplitude). Multiply both components by g/(1+(g-1)*m), with g=1,2,4,8. The dominant output becomes g*m/(1+(g-1)*m). Zero remains zero, full scale remains one, and the curve is strictly increasing without a clipping plateau or nonzero floor. Frequencies, sample times and side independence remain unchanged. Both bands share the multiplier, preserving their instantaneous ratio. Envelope shape and contrasts between different strengths do change. This is not measured response compensation or a combined two-band voltage limit.

Parameters 2/4/8 are engineering test choices, not patent constants. There is no per-effect normalization, invented high-frequency component or onset kick. No enhanced curve is installed in the game or made the SDL default. A calibrated profile requires measured frequency/amplitude response and transient checks; this catalog predominantly tests 160 Hz and cannot validate other carriers.

Manual comparison: small, middle, large, then sustained small; compare original, light, medium and strong. Check distinct strengths, attack/tail and complete stopping. With only Switch 2 Pro this establishes preferred target-device tuning, not fidelity to an original Switch controller.

Validation: four offline tests cover monotonicity, range, silence, endpoints, identity, invalid values, stereo independence and band ratios. All 76 catalog entries validate under all four curves. A fake-device GUI check covers profile routing and adaptive layout at 200% DPI. No physical vibration was triggered.
