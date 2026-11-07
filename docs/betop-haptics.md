# Betop KP70C haptics (Windows)

The local SDL3 checkout supports KP70C wired (`20bc:5173`) and receiver
(`20bc:5174`) modes through the controller's separate vendor HID interface.
Savage Wind uses the original game's rumble for the grips and adds authored
trigger/shoulder pulses for verified Player contact-effect events, plus sustained
bow/guard resistance sampled from the game's Player state. The official
Betop AI assistant and its game recognition models are not required.

## Behavior and scope

- Grip output follows all original mixed game vibration. The KP70C report
  represents left/right intensity envelopes, not Switch dual-band frequencies;
  this is not lossless HD rumble reproduction.
- Extra effects are driven by accepted named contact-effect requests whose actor
  name is exactly `Player`. No keyboard/gamepad button is interpreted as proof
  that an action happened. Weapons, NPCs and environmental events continue to
  use their original grip rumble without fabricated semantic effects.
- `NormalHit` and `ArrowHit` have short right-side effects; `Guard` and
  `GuardJust` have distinct left-side effects; heavy impacts, landing and selected
  stasis events have short bilateral or alternating profiles. Profiles are
  authored in `runtime/betop_haptics_profiles.h`, not extracted Nintendo actuator data.
- Shoulder events now use the physically verified direct-drive on/off path.
  Their duration, side and priority remain authored per event; the earlier
  crisp/soft/heavy preset selections were ineffective in physical tests and
  no longer determine game output. Trigger pulses still use mode 2. A separate
  held-trigger diagnostic confirmed clear vibration on both sides and normal
  recovery; the short in-game contact pulses still need scene-level validation.
- Sustained bow resistance follows the native charge-animation predicate, and
  shield resistance follows the native Player guard bit. Charge progress is
  not mapped: bow force is steady at 15, shield force at 10 (start parameter 10).
  This is a first integration, not full bow-type or combat-action coverage.
- Focus loss, vibration prohibition, game reset and disconnect discard pending
  enhancements. Short leases stop outputs on a separate SDL thread even when
  the game's event loop stalls. Normal exit and diagnostic early-exit paths
  close the gamepad and flush neutral reports before terminating.
- The device has no verified firmware watchdog in this protocol. Forced process
  termination, OS failure, device disconnection and physical stopping behavior
  cannot be guaranteed by a host-only lease.

`SAVAGEWIND_BETOP=0` disables the additional semantic trigger/shoulder effects.
Original game grip rumble remains enabled. Do not run another program that writes
AI effects to the same controller concurrently: the SDL ownership check only
coordinates other copies of this SDL implementation, not the official assistant.

## Build and run

Use the modified sibling SDL3 checkout and the normal Windows build guide.
The app imports `SDL_SetGamepadBetopEffects`; a stock SDL3 DLL cannot supply it.
Keep the matching `SDL3.dll` beside `build/savagewind.exe`.
For normal play, open `build/savagewind.exe` directly in Explorer. It uses the
extracted files, settings and saves in `build/`. From a terminal, start it from
that directory and pass any custom launch arguments to the executable.

Regenerate the current game's `main` with `tools/gen.py` after changing the
event hook, then rebuild the runtime and game library. Do not edit generated C.
The hook is verified against both supported build IDs:

| Game | Accepted-effect call | Original callee |
| --- | --- | --- |
| 1.8.2 (`de0b6ac4efc1dda7`) | main + `0x10e2db8` | main + `0xe30a30` |
| 1.9.0 (`cd57b23fa4bbad65`) | main + `0x11f6b48` | main + `0xf44080` |

The generator checks a masked instruction pattern, preserves the original call
and link register, and rejects a changed context. At the hook, `x19` retains the
event (`RumbleType` at +`0x90`), and `x21` retains the actor (name pointer at +16).
This is an accepted **contact-effect request**, not proof that a final native
vibration has already occurred. The original effect dispatch is unchanged.

## Verification

From the project root with the documented MSYS2 toolchain:

```sh
make test-vibration test-betop-haptics test-input-focus
make test-betop-game-state test-betop-game-state-runtime
tools/venv/Scripts/python.exe tools/test_game_sites.py
tools/venv/Scripts/python.exe tools/test_betop_sites.py
tools/venv/Scripts/python.exe tools/test_betop_state_sites.py
```

SDL's `testbetophaptics_protocol` exercises packet encoding and finite leases;
`testbetophaptics_api` uses virtual devices to check fallback, stop, expiry,
validation and unsupported devices. Both perform no physical device output.

`testbetophaptics --list` enumerates without sending haptic output reports.
`testbetophaptics --sequence` requires one unambiguously identified KP70C and
sends six low-strength 250 ms effects: left/right grip, left/right shoulder,
left/right trigger. Each waits without event pumping, then sends explicit stop.
Trigger feedback may need a light trigger press to feel. Successful host writes
are not a substitute for confirming the actuator response by touch.

As tested on 2026-11-02: Windows x64, receiver `20bc:5174`, vendor usage `00ff:3`.
Default discovery and the six-channel output sequence completed without reported
transport errors. Subsequent user feedback confirmed grip vibration only;
shoulder and trigger pulses were not felt. Those outputs are not physically
validated; successful host writes do not establish six-channel operation.
In a subsequent interactive resistance-mode test on the same date, the user
confirmed resistance on both triggers and normal recovery after the test ended.
Later per-trigger testing confirmed clear vibration (mode 2) on both sides,
with normal recovery. Breakthrough (mode 3) was weak on the left and out of sync
on the right, with normal stopping. Subsequent prearming plus the observed
two-second refresh cadence produced clear, responsive breakthrough feedback
on both sides, with normal recovery, in a 12-second per-side test. The matching
SDL DLL has been updated in the runnable build. Breakthrough is not used by
the current contact-event profiles; this diagnostic does not validate bow logic.
Subsequent shoulder-only tests confirmed no perceived response on either side,
both with light touch and while held down (three 300 ms low-crisp presets per
stage, host exit zero). Those shoulder presets failed physical validation.
A subsequent SDL direct-drive diagnostic on the same date produced clear
left and right shoulder feedback, confirmed by the user (three 100 ms pulses
per side, fingers resting, host exit zero). The user also confirmed stopping
with no residual vibration. The runtime was then switched from preset-only
shoulder output to direct drive, preserving event durations and side priority.
The event scheduler, vibration and input-focus tests passed, and the Windows
executable was rebuilt. In-game actuator timing still requires manual validation.
In-game combat behavior also remains unverified. The local runnable game was
built from the existing 1.8.2 files; the 1.9.0 hook was checked statically.
A hidden Vulkan startup check loaded the rebuilt 1.8.2 game library, detected
the XInput controller and exited successfully after 90 frames. It used a
separate diagnostic save directory and did not validate combat effects.

## Protocol provenance

Report layouts were independently implemented from static observations of the
installed Betop software; no vendor executable, source, models or game assets
are included. The examined detector SHA-256 was
`7d9e93d302ca9103d103eee4ae415b302680f47c37358d405e542bda6e8c299a`.
The matching `kp70c.js` supplied independent report-layout context.
The companion uses 64-byte transient reports (`02 16`, `02 56`, `02 76 01`),
never configuration-save, reset, mode-switch, lighting or firmware commands.
The full private investigation evidence stays in ignored build/local analysis
directories and must not be committed with game-derived artifacts.

## Breath of the Wild haptics design v1

This is the game-owned implementation plan, dated 2026-11-02. The earlier
scratch proposal in SDL's ignored research directory is historical reference;
this document and the compiled game profile table are the maintained policy.
Hardware validation establishes actuator operation, not gameplay integration.

### Ownership and evidence

| Layer | Owns | Must not own |
| --- | --- | --- |
| Game hook (`tools/game_sites.py`, `tools/patches.py`) | Version-checked actor/action observations, original call preservation | HID packets, device timings |
| Game policy (`runtime/betop_haptics_profiles.h`) | Semantic event selection, side, duration, priority, authored trigger strength | Vendor command IDs, controller firmware assumptions |
| Game scheduler (`runtime/betop_haptics.c`) | Player filtering, event lifetime, per-side arbitration, focus/reset, full desired effect state | Vendor retransmission cadence, HID opening |
| Original vibration (`runtime/hle/vibration.c`) | Nintendo mixed left/right low/high values, permission, timely submission | Inference of bow draw from vibration amplitude |
| SDL3 | Device/container matching, actuator mode encoding, validation, leases, queue, refresh, disconnect and close | Bow type, shield state, combat priorities, game offsets or assets |

The game uses named SDL trigger/shoulder modes. Direct shoulder drive uses the
SDL constant `SDL_BETOP_SHOULDER_DIRECT_ON`; the protocol representation stays
in SDL. The game submits a full trigger/shoulder snapshot, not independent calls
that accidentally clear other actuators. Grip output remains a separate stream.

Receiver `20bc:5174` has physical evidence for grips, both shoulder direct
outputs, both trigger resistance/vibration modes, and prearmed breakthrough
with periodic refresh. Normal stop was confirmed for the enhanced modes.
Wired `20bc:5173` is code-supported but not covered by these physical trials.
Follow mode and shoulder presets have no positive physical validation and are
excluded from this design. The software never requires vendor AI recognition
or model files at runtime.

### Content-to-effect mapping

Status `integrated` means hooked and tested in a harness, not felt in gameplay.
Status `planned` means no reliable game state is wired yet; it is not enabled
through guessed input or timer rules. All numeric settings below are authored
starting points in protocol units, not physical force measurements.

| Content | Required observation | Effect | Current status |
| --- | --- | --- | --- |
| Native impact, movement, environment | Original game vibration submission | Preserve the original grip envelopes; SDL chooses device representation | Integrated; game scenes need verification |
| NormalHit / ArrowHit | Accepted Player-owned contact request | Right shoulder direct pulse, 90/70 ms; right trigger vibration strength 15/12 | Integrated; ArrowHit is not proof of arrow release |
| ImpactHit / MonsterPunch | Accepted Player-owned contact request | Bilateral shoulder pulse 160/150 ms; trigger vibration 25/20 | Integrated; do not infer whether Player attacked or was struck beyond the proven event |
| Guard / GuardJust | Accepted Player-owned contact request | Left shoulder pulse 100/180 ms, trigger vibration 18/25; perfect guard gets higher priority | Integrated; does not establish continuous guarding |
| Stasis contact/start/end | Accepted Player-owned BitaLock request | Bilateral 100 ms contact; left/right 90 ms start/end cues | Integrated under strict actor filter; object-owned interactions are not falsely claimed |
| Wind barrier, weapon raise, tree interaction, landing | Accepted supported Player-owned request | Short side-specific shoulders; retain native grip vibration | Integrated; exact values live in the profile header |
| Bow draw / full draw | Native upper/lower charge AS state | RT resistance mode: start 10, steady strength 15; future verified progress may ramp 10 to 25 | Charge-state integration delivered; full-draw progress/arrow inventory not separately mapped |
| Arrow fired / draw cancelled | Leaving the charge AS state | Clear RT resistance on either exit; retain original native grip output | State-exit clearing delivered; separate projectile-release/cancel identification and extra release cue remain planned |
| Shield raised | Native Player guard bit, not raw ZL | LT resistance, start 10 and strength 10 | Guard-state integration delivered; physical scene verification pending |
| Charged melee / weapon throw | Actual action start, progress, release and cancel | Grip preserves native charge rhythm; brief right shoulder release cue; trigger only if that physical control participates | Planned; no phantom force during unrelated button actions |
| Bomb / stasis / magnesis / cryonis | Actual rune selection, successful activation, interaction and cancel | Event-specific shoulder cues; resistance only for a proven continuous interaction | Planned; no generic L-button activation cue |
| Walking, climbing, swimming, riding, shield surfing | Native vibration first; actual locomotion state for any enhancement | Keep native grip rhythm; additional footstep vibration off by default | Native path integrated; optional enhancement planned |

A single stable bow-resistance profile is preferred initially. Bow material,
draw duration, multishot and arrow element variations require verified game
data and tuning. Damage stat is not treated as draw weight. Elemental impacts
and explosions belong to actual hit/explosion events, not arrow release.
Breakthrough is available for a deliberate detent in a future throwing/rune
interaction, but is not the default bow effect: a bow needs continuous tension.
Stable full-draw resistance is preferred over perpetual vibration and fatigue.

### Sustained action state machines

Bow: `Idle -> Drawing -> FullyDrawn -> Fired/Cancelled -> Idle`.
Shield: `Idle -> Guarding -> Impact/PerfectGuard -> Guarding/Idle`.

- Arm the appropriate trigger when the actual action becomes available/active,
  before the finger crosses its intended resistance or breakthrough region.
  Physical axis travel can constrain an effect, but never identifies the action.
- Use game draw progress or a verified weapon draw duration. If neither is
  available, do not manufacture a full-draw transition with a universal timer.
- Release and cancellation are separate observations. No-arrow attempts,
  telescope use, quick menus, weapon changes and cutscenes must not inherit bow
  resistance. Airborne slow motion follows game action progress; host leases
  still use real monotonic time.
- Menu/pause, death, scene reset, focus loss, vibration disable and disconnect
  clear enhancements. On resumption, rebuild from fresh current game state;
  do not replay old contact events or reactivate an obsolete draw.
- The current sampler implements charge-active/guard-active versus inactive,
  not all transitions above. It does not distinguish full draw, Fired versus
  Cancelled, or bow types. Verified state reads feed steady force and clear it
  on exit; progress-specific effects require further state evidence.

### Composition, timing and comfort

The current contact scheduler resolves per-side priority and extends repeated
same-type events. The planned sustained scheduler must arbitrate triggers and
shoulders independently: a short impact must not erase a valid bow hold.
For each trigger, choose one mode; resistance and vibration are not assumed to
mix. A transient interruption must restore only a still-valid action state.
Stop/disabled state overrides everything; sustained action takes precedence
over unrelated contact pulses, while perfect guard outranks ordinary impact
on the same channel. Direct shoulder cues use duration and spacing, not an
unproven intermediate drive value. Retain short finite pulses and tune repeated
impact duty cycle in gameplay before broad release.

Submit active desired state with a 100 ms maximum lease, renewed at most every
50 ms and before the next event boundary. A future action-state loop can use
25 ms updates for smoother ramps. SDL owns its approximately two-second
unchanged-command refresh; the game must not toggle modes to keep effects alive.
Original grip output must not be doubled by fabricated impact vibrations.
Settings should eventually separate native rumble, trigger assistance and
shoulder cues; currently only the existing `SAVAGEWIND_BETOP=0` enhancement
switch is implemented. A future strength slider may scale validated trigger
parameters but must not pretend the binary shoulder command has linear gain.

Physical mapping defaults to LT/ZL and RT/ZR. Action force must follow the user's
actual binding when rebinding is supported, rather than remain attached to a
hard-coded physical side. No continuous force is added to ordinary running or
climbing, and no heartbeat is inferred from screen color or audio volume.

### Delivery and acceptance

1. Baseline delivered: device modes in SDL; named game profiles; contact hook,
   filtering, expiry, focus/reset, and shutdown; matching Windows executable/DLL.
2. State sampler now delivered: trace bow/guard transitions and check normal shot,
   no-arrow attempt, cancellation, weapon switch, menu, telescope, death,
   focus change, airborne slow motion and scene reload on 1.8.2. Validate
   1.9.0 independently; its existing contact hook is only statically checked.
3. Bow/guard force is enabled for the verified native state predicates.
   In a visible diagnostic, show the action, output side/mode and stop reason.
   Repeated drawing must remain responsive beyond 12 seconds and every exit
   path must remove force. Verify held force and short impacts coexist.
4. Tune contact effects in representative combat, perfect guard, landing and
   rune scenes. Record native grip output to avoid duplicate effects. Add
   optional world/rune cues only when their state and actuator response pass.

For each physical case record game build, Windows version, controller/transport,
binding, scenario, expected and perceived effect, latency/continuity and stop.
Automated tests cover data validation and state transitions; they cannot certify
feel. No packaged vendor models, game dumps, keys or derived game code are
needed to maintain this authored plan or its profile table.

### Native-state integration record

`runtime/betop_game_state.c` is a bounded read-only decoder; the host adapter in
`runtime/betop_game_state_runtime.c` samples it before the existing enhancement
scheduler. It does not execute guest functions or alter game memory. The adapter
requires a supported game version, all six strict layout proofs, a unique
supported SDL gamepad, gameplay focus and active game-frame progression.
Windows reads use ReadProcessMemory with main-module/heap range limits; failed
or short reads clear the action snapshot. The decoder rechecks Player and ASList
identity. The snapshot is not atomic against all animation updates; expiry and
fresh per-frame sampling limit transition staleness rather than promise zero.

| Proof | 1.8.2 main offset | 1.9.0 main offset |
| --- | --- | --- |
| Native charge predicate | `0xbcd19c` | `0xce067c` |
| Upper AS comparison | `0xb4f564` | `0xc62c74` |
| Lower AS comparison | `0xb50f84` | `0xc64694` |
| AS group/slot lookup | `0x138bee0` | `0x14a0040` |
| Active AS name lookup | `0x13a6f5c` | `0x14bb0bc` |
| Player guard predicate | `0xbcca4c` | `0xcdff2c` |

The charge predicate checks upper group 1/slot 1 for `BowCharge` or
`SquatBowCharge`, and lower group 0/slot 0 for `WallBowChargeL/R`. Similar
`BowShoot`/`BowReload` predicates are explicitly rejected by string-identity
validation. The decoder follows PlayerInfo +0x60 to the Player, verifies its
exact actor name, then Player +0x560 to its ASList. Group and slot sizes/offsets
are proven by both versions' getter code, not inferred from camera state.
Guard is Player +0xc40 bit 4; the two versions' predicate bytes agree. Its
identity is corroborated by the Player vtable/getArmors/getRideInfo neighborhood,
guard-angle method and native damage/animation callers. The public
[zeldaret symbol inventory](https://github.com/zeldaret/botw/blob/master/data/uking_functions.csv)
provided naming cross-reference only; its 1.5.0 addresses were not used as runtime
offsets. Native analysis artifacts remain under ignored `build/betop-rea`.

All six patterns must match uniquely, even for a KNOWN build; table fallback
is refused. The generator also verifies charge strings and the helper call
relationships before emitting layout data. Unknown later versions fail closed.
Input does not select the action. No charge-time estimate or arrow-release
effect is invented. Bow/guard force wins its own trigger while independent
contact shoulder cues remain available. Default physical mapping is RT bow,
LT shield; custom action-to-physical-trigger remapping is not implemented yet.

Pause/map/quick menus, title/loading, interaction events/cutscenes, movies,
focus loss, disabled vibration, guest unwind, unsupported layouts and invalid
reads suppress enhancements. A guest frame stalled beyond 150 ms also suppresses
force even when host input remains responsive. Fresh action snapshots expire
after 150 ms; SDL receives at most 100 ms leases. Resume requires fresh samples.

Set `SAVAGEWIND_BETOP_TRACE=1` for `[betop-state]` transitions (valid, bow, guard,
gameplay), then launch the game normally. Local interactive wrappers and their
session logs are optional diagnostic aids and are not versioned. No gameplay
input is automated. Validate no-arrow draw, scope, weapon changes, death,
cancel/fire, pause and focus transitions in the actual game before declaring
those scenarios passed. Existing hardware mode tests do not replace this step.

Integration validation on 2026-11-02: all 95 sites and the sampler helper/string
relationships matched both local game versions. Synthetic sampler, adapter,
sustained/contact scheduler, vibration, input-focus and generator checks passed.
The 1.8.2 Windows executable/game library rebuilt successfully; an isolated-save
hidden Vulkan startup check loaded 95 sites, detected the controller and exited
with status 0. Enhanced output was disabled for that smoke check. No bow/guard
scene or physical in-game output was verified by the startup check.

User acceptance deferred on 2026-11-02: the user will test when normal play
reaches bow/shield content. Do not mark in-game haptics as passed or automatically
start further hardware/game tests. Resume from the existing runnable build and
the manual scenarios above when the user reports results or requests testing.
