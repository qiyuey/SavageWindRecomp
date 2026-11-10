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

Do not stage these files. The `rumble_catalog_player.py` command above starts the GUI. Search by name, source or ID, select an effect, inspect its curve and source, then press Play. Escape, Stop, focus loss and closing request silence. The player never starts vibration just because a row is selected.

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

### Breath of the Wild catalog

The current main-module native controller preset table has six entries: three one-cycle sizes and three patterns used by duration-based requests. The event action definitions and native dispatch agree on those families. Export executes the original ARM64 addon update in Unicorn and intercepts the final HID value, avoiding a handwritten approximation of its envelope. The default constructor scale, isolated initial state and a nominal 120 Hz timebase are recorded explicitly.

The first complete local scan covered 8,363 source files, 8,655 archives/subarchives and 720 unique AI programs with no reported parser errors. It found 36 controller behavior/action definitions and 556 flow/timeline controller call records, including duplicate packaging. These yield 70 distinct repeat/duration combinations in addition to the six base presets. Current extracted RomFS coverage is not a claim to cover absent DLC content or every reachable runtime event.

The earlier captured weapon-swing amplitudes are all represented by the native exported envelopes. This validates their discrete values for those captures; it does not validate every game context or actual device output timing.

## Limits that remain visible

- Static definitions are not the same as every possible game-state combination. Distance, runtime configuration, repeated interruption, priority suppression and concurrent sources can change what actually plays.
- Resource strings and nearby animation names are provenance/context hints, not automatically reliable semantic labels for an effect.
- Duration-based variants are standalone fixtures using their resource parameters; replay does not reproduce the entire cutscene's scheduling, forks or other actors.
- Camera shake and XLink contact types are not native controller effect definitions and are excluded.
- Samples are source requests. The host scheduler, SDL write cadence and USB transport can coalesce or delay them; API acceptance is not a physical measurement.
- Switch 2 output still uses the experimental patent frequency mapping. No claim of calibrated HD1-to-HD2 perceptual equivalence is made.

The workspace workflow and IDA repair notes are in the [Switch research workflow](../../docs/switch-research-workflow.md) outside this repository.

The device dropdown lists SDL-recognized gamepads with name, VID/PID and instance ID. Initial selection prefers Switch 2 Pro; Refresh preserves a connected selection. Refresh and device changes stop playback, and reconnecting after an empty startup or disconnect does not require restarting the tool. Explicitly select a device after reconnecting and press Play. Enumeration does not establish support for frequency-aware rumble on every controller. Playback records include the selected device identity.

Device selection was checked on Windows using fake SDL enumeration/output (empty startup, refresh, two devices, routing, disconnect and cleanup), plus the real Tk layout at desktop DPI. These checks do not constitute physical-controller acceptance.

Offline catalog, routing and strength checks use synthetic samples and a fake device:

```powershell
tools/venv/Scripts/python.exe -m unittest discover -s tools -p 'test_rumble_*.py'
```

## Runtime output

The game and catalog player default to 50% strength. At 0% output is silent,
20% uses the game's requested amplitude, and 50% and 100% apply gains of
2.5 and 5. Shared headroom preserves the low/high ratio on each side when
the stronger band reaches full output. SDL receives the resulting values
and handles device-specific encoding and USB scheduling.

The game publishes individual HID 201 values and complete HID 206 stereo
batches to a dedicated worker. The worker wakes when a new state arrives,
renews unchanged active output every 50 ms, and clears stale requests after
100 ms without an event-thread heartbeat. A complete stop survives a newer
active request. Focus loss, mute, disconnect, reset and shutdown clear the
active state. The Windows worker uses a high-resolution waitable timer for
input polling and retries. Active states can coalesce before device output.

## Verification

Run `make test-vibration` and `make test-input-focus` for synthetic coverage of
HID batches, routing, strength, the worker, input focus and device recovery.
Build `build/test_vibration_device` and run it with no argument for device
inventory. With `--play`, it sends one second of low-amplitude synthetic
stereo effects to Switch 2 Pro (057e:2069), then stops. Set
`SAVAGEWIND_VIBRATION_TIMING=1` to record application timing alongside SDL's
HID trace. Keep the resulting logs local.

The 2026-11-10 Windows x64 test on Switch 2 Pro over USB recorded successful
active transfers and a successful final stop transfer. The effect used
160/320 Hz on one side and 180/360 Hz on the other. Physical response and
in-game effects require separate checks.
