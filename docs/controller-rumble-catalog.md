# Controller rumble catalog and local backtesting

This tool derives a local catalog from the player's own Breath of the Wild 1.9.0 files. It does not contain game effect strings, game machine code, or generated waveforms in the repository.

## Strength policy update (2026-11-06)

The BOTW runtime and Python catalog now apply 5x at 100%, 2.5x at 50%,
and original amplitude at 20%. The Ocarina catalog applies 1x at 100%.
Each application scales once with shared per-side headroom; SDL receives final
normalized amplitudes and no longer reads the experimental driver gain hint.
The native game defaults/resets to 50%. Older 10x descriptions below are
historical where explicitly labeled. No native binaries were rebuilt for this
update. See the workspace SDL3 `docs/README-rumble-strength.md` contract.

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

### Optional BNVIB resource catalog

The same Python GUI also loads `build/rumble-catalog/oot-catalog.json` when
present. Choose the game in the catalog selector. Resource names are preserved;
search includes explicit call-record names. Both frequency-component amplitude
curves are displayed. Audition routing can select both sides, left or right;
this does not claim the original game uses that routing. Changing game, routing,
selection or strength stops playback and never starts another effect.

`tools/export_bnvib_catalog.py` takes local `--resources`, `--sdk`, `--calls`
and `--output` paths. It executes a hash-verified SDK parser and decoder in
Unicorn, with an instruction budget, rather than embedding game code, tables or
waveforms. Unsupported SDK builds and malformed sample extents are rejected.
The output JSON and adjacent Markdown summary are derived private content and
must remain in the ignored build directory.

BNVIB resources retain their 200 Hz sample times, both amplitude/frequency
components and loop metadata. Audition plays each complete resource once and
adds a final stop; loop metadata does not automatically repeat the effect.
The call table is preserved, including records without an explicit resource
association. Runtime gain, duration, default values, mixing and side routing are
not inferred or applied. This is HD1-style parameter playback to an HD2
controller, not native HD Rumble 2 emulation. Strength keeps the existing
game-specific convention: Ocarina 100%=original, BOTW 100%=5x (20%=original),
with shared per-side saturation.

Offline checks, using synthetic fixtures and a fake device:

```powershell
tools/venv/Scripts/python.exe -m unittest discover -s tools -p test_bnvib_catalog.py
tools/venv/Scripts/python.exe tools/test_rumble_amplitude_mapping.py
```

### Breath of the Wild catalog

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

Responsibilities: settings/UI own the percentage; runtime/rumble_strength.h owns application strength scaling and shared per-side headroom; vibration_update applies this only to its output copy after reading the game's original values. SDL owns device selection, frequency/amplitude encoding, USB scheduling and device-specific response. No Switch 2 encoding or patent formula is duplicated in the game. Raw tracing and HID readback remain unchanged. The Python player's scale was revised on 2026-11-06: BOTW 100% = x5 and Ocarina 100% = original; the native BOTW runtime now also uses 100%=5x with a single shared-headroom scale. This does not claim perceptual equivalence between controllers.

The game enables the experimental Switch 2 frequency path by default (the explicit SDL_JOYSTICK_HIDAPI_SWITCH2_EXPERIMENTAL_HD opt-out is retained). The new SDL source ignores the former HD_GAIN hint entirely. Applications submit final amplitudes; no additional curve is applied. Existing deployed binaries predate this source change and must be rebuilt before native behavior changes.

0% silences ordinary vibration and gates Betop semantic companion effects; positive strength scales the ordinary low/high vibration bands, not Betop trigger resistance or binary shoulder actuators. Frequency, timing and original HID values are preserved. Saturation can flatten strong peaks, while maintaining the ratio of both bands on each side.

Validation: make test-vibration covers immediate preference changes, mute, unchanged HID readback, stereo/frequency routing, leases, focus/disconnect and shared saturation. build/test_rumble_settings checks missing-key default, saved 0/100 across processes, range clamping and preservation of a disabled rumble toggle using a separate test INI. The canonical Windows executable was rebuilt with RELEASE=1 and its GUI subsystem verified. Actual Windows/Vulkan menu appearance and controller/gameplay acceptance remain manual; the game was not automatically launched.

The Python player strength slider covers 0–100% and defaults/resets to 50%.
As requested on 2026-11-06, its full-strength gain depends on the selected game:
Ocarina 100%=original amplitude (50%=0.5x); BOTW 100%=5x (50%=2.5x,
20%=original). Switching games updates the multiplier and hint immediately.
This is the user's audition convention, not an assertion that every Switch 2
game has the same native vibration scale. Apply gain * percentage / 100 once.
Shared per-side saturation preserves the two-component ratio; frequencies,
timing and original catalog samples remain unchanged. There is no additional
amplitude curve or selector. Game/routing/strength changes stop playback and
require an explicit Play. Logs retain the applied base gain. The native BOTW runtime uses the same 5x full-strength convention; SDL does not amplify the final request.

The following device-curve comparison describes an earlier experiment; its
selector and nonlinear curves have since been removed from the Python GUI.

The amplitude selector defaults to the linear device curve after user strength scaling. Three optional, uncalibrated curves are restricted to Switch 2 Pro (057e:2069). Select a curve, then explicitly press Play. Changing the curve requests Stop. Original catalog samples remain unchanged; the graph overlays the output envelope and displays both peaks. Start records include the profile and curve parameter.

For each side, let m=max(low amplitude, high amplitude). Multiply both components by g/(1+(g-1)*m), with g=1,2,4,8. The dominant output becomes g*m/(1+(g-1)*m). Zero remains zero, full scale remains one, and the curve is strictly increasing without a clipping plateau or nonzero floor. Frequencies, sample times and side independence remain unchanged. Both bands share the multiplier, preserving their instantaneous ratio. Envelope shape and contrasts between different strengths do change. This is not measured response compensation or a combined two-band voltage limit.

Parameters 2/4/8 are engineering test choices, not patent constants. There is no per-effect normalization, invented high-frequency component or onset kick. No enhanced curve is installed in the game or made the SDL default. A calibrated profile requires measured frequency/amplitude response and transient checks; this catalog predominantly tests 160 Hz and cannot validate other carriers.

Manual comparison: small, middle, large, then sustained small; compare original, light, medium and strong. Check distinct strengths, attack/tail and complete stopping. With only Switch 2 Pro this establishes preferred target-device tuning, not fidelity to an original Switch controller.

Validation: four offline tests cover monotonicity, range, silence, endpoints, identity, invalid values, stereo independence and band ratios. All 76 catalog entries validate under all four curves. A fake-device GUI check covers profile routing and adaptive layout at 200% DPI. No physical vibration was triggered.

## Game-side 4 ms submission worker (2026-11-06)

Native BOTW grip output now has a dedicated worker. HID 201 publishes the
latest value and HID 206 publishes its entire stereo batch under one state
lock. Changed nonzero output targets a minimum 4 ms start-to-start period;
unchanged active output retains the 50 ms renewal and 100 ms SDL lease.
Intermediate active states can coalesce when producers run faster than output;
this is not lossless replay of sub-4-ms pulses. Complete silence is latched
until submitted, including when a new active value follows before a worker wake.

Windows uses an interruptible high-resolution waitable timer. Initial testing
with winpthreads timed waits observed approximately 15.6 ms instead of 4 ms;
that wait is not used on Windows. Other hosts use a condition-variable wait
with monotonic output gating, and were not runtime-tested for this change.
SDL_UpdateJoysticks is serviced by the worker as well: the Switch 2 driver's
nonzero requests otherwise wait for a device update before entering its queue.

The state lock is released during SDL submission and device updating. A separate
output lock serializes device use, detach and close. Thus a slow SDL call does
not hold the game publisher's state lock. Device-management/event-thread work
may still wait for an in-flight output call. The game does not wait for USB
transfer completion. Stops cannot preempt a call already inside SDL.

Only the event thread reads settings and samples guest state for Betop companion
effects. The worker uses a synchronized focus/strength snapshot. A missing
event-thread heartbeat for 100 ms clears grip state and stops renewal. Focus
loss, permission denial, reset, detach and shutdown request silence. Failed
submissions retain their target and retry at a bounded 4 ms cadence. Successful
older calls cannot clear a newer stop request published while SDL was busy.

Validation on Windows x64:

- `make test-vibration`: existing HID/routing/strength tests and the new real
  worker/mock SDL harness passed. The latter covers idle-event-loop scheduling,
  10,000 stereo batch publications, latest-state output, SDL calls deliberately
  held open while publishers continue, stop preservation, active/stop retries,
  mute, focus, heartbeat expiry, reset, detach and worker restart. Three final
  mock runs measured mean active intervals 4.214, 4.222 and 4.180 ms, with
  minima at least 4.010 ms and maxima at most 4.542 ms. These are application
  scheduling measurements, not USB or actuator measurements.
- Input-focus, PC controls, Betop effect/state/runtime tests and the gyro-binding
  harness passed. `make RELEASE=1 -j8 build/savagewind.exe` rebuilt the app.
- A one-second low-amplitude synthetic test using the earlier integration and
  the connected 057e:2069 controller submitted 237 active states with no SDL API
  failures. Active application intervals averaged 4.223 ms (min 4.000, max
  7.998 ms). This run lacked enabled SDL input-category logging, so it did not
  establish USB write results. The device was no longer enumerated for the
  follow-up; the final lock/stop refinements have not been retested on hardware.
  Physical sensation, physical stop, input continuity and BOTW gameplay remain
  manual acceptance items. No game was launched and no saves were used.

For a local read-only device inventory, build and run
`make build/test_vibration_device` then `build/test_vibration_device`.
Explicit `--play` runs a one-second low-amplitude test only on a 057e:2069
controller, enables SDL HID write tracing, then requests stop and detaches.
`SAVAGEWIND_VIBRATION_TIMING=1` additionally records application submission time,
latest publication sequence/age, active state and API acceptance. SDL acceptance
alone does not prove physical delivery. Keep resulting device logs local.


## Ordered asynchronous SDL deployment (2026-11-06)

The Windows build now carries SDL3.dll `8F40DCD19C88785EA07141AA28494DF14CB8B78FB3B0951B915B3EC07ECC32FF`. USB Switch 2 Pro uses two bounded
in-flight writes, immutable buffers and FIFO completion retirement. Stop is a
cancel/drain/zero-acknowledgement barrier; the public frequency API and application
strength scaling are unchanged. The old DLL backups were subsequently deleted at the user's request.

The actual runtime-worker/device harness passed with the deployed DLL. ETW
matched 245 writes, verified ordered firmware sequences and FIFO retirement,
and confirmed final silence. Its steady completion interval averaged 4.099 ms;
Windows jitter can produce intervals around 8 ms. Runtime/HID, focus, Betop and
PC-control regressions passed. The stereo concurrency test now waits for actual
output and renews its foreground heartbeat rather than relying on an 8 ms sleep.
Physical unplug/replug and gameplay sensation remain manual. See the workspace
SDL3 rumble research document's production transport section for full evidence.


## Event-driven publication update (2026-11-06)

The application owner now wakes on new scalar/stereo values with no healthy 4 ms
send gate. Input service, lease renewal, expiry and failed-output backoff remain
timed. The existing 100 ms heartbeat, stale-value clearing and serialized device
close remain in force. The SDL transport is also event-driven with two in-flight
reports, each carrying both sides and bands. In the actual runtime/USB 4 ms input
probe, 250 main-effect requests completed in order with mean kernel spacing
3.998 ms and maximum4.119 ms. Mock publishers at 1 ms forwarded without a forced
4 ms tick. Stop/focus/Betop/control regressions passed; gameplay feel and real
unplug remain manual. No prior DLL copies were recreated.
