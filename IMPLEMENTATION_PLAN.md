# Ambiance Generator — Implementation Plan

Status: implemented as the local Apple Silicon first release, including the motion-controlled drone. See `README.md` for use and `docs/verification.md` for measured release checks. The design and delivery sequence below are retained as the original technical basis.

Basis: `Ambiance_Generator_Codex_Spec.md`. That document supplies the product vision and feature requirements. This plan established the sequence and behavior contracts used for implementation. The confirmed release scope supersedes its provisional assumptions: external-editor Lua authoring, this Mac as the supported platform, collision notes plus continuous drone, and tested local delivery.

## 1. Product outcome and working assumptions

Build a native audiovisual instrument in which one scripted world produces both moving images and sound. The first useful workflow is:

1. Open an example Lua scene.
2. Watch and hear the scene in a native preview.
3. Change the script in an external editor and reload it.
4. Pause, resume, or restart without losing synchronization.
5. Export the same composition as a video and, optionally, a separate WAV.

The first technical demonstration is bouncing circles that produce bell sounds. A successful product must also produce an interesting composition: pleasing motion, controlled sound levels, repeatable results, and a clear edit/preview/export loop.

Working assumptions until the product questions are answered:

| Decision | Planning default | Effect of a different answer |
|---|---|---|
| Initial machine | This Apple Silicon Mac | Distribution adds packaging, signing, and installation work; other operating systems add build and device testing. |
| Initial authoring | Lua in an external editor | Sliders add parameter declaration, validation, change timing, and export persistence. A mostly visual workflow needs a separate UI design phase. |
| First demonstration | Collision-triggered musical events | Continuous ambient sound is the first expansion; move it into release acceptance if ambient scenes are the main goal. |
| Preview size | 1280×720 logical scene, resizable window | Window size never changes physics coordinates. |
| Main output | MP4 with H.264 video and AAC audio; optional stereo WAV | Other codecs and archival formats follow after the basic export works. |
| Export length | Short tests first; stream the implementation so longer pieces remain possible | Multi-hour work requires additional endurance tests and large-file audio support. |
| Runtime services | Entirely local, no telemetry or cloud account | No runtime network dependency is needed. |

Three product questions have been sent to the user: ambient versus event-driven scenes, preferred authoring workflow, and who must run the first release. These affect release scope; they do not prevent designing the engine.

## 2. Changes to the original approach

1. **Prove timing and export earlier.** After a minimal physical scene, generate a short WAV and synchronized video before expanding the editor or effects. This tests the defining promise before substantial polish work.
2. **Use one engine for preview and export.** A device-independent core advances the world and schedules sound. Separate drivers decide whether to run interactively or as fast as possible.
3. **Start with one software drawing path.** Render to an offscreen canvas for both preview and export. Upload that canvas into the preview window. This simplifies headless export and reproducible frame testing. Benchmark it before committing to performance claims.
4. **Keep output resolution separate from scene geometry.** A 1920×1080 export of a 1280×720 scene scales the image; it does not move the walls or change the composition.
5. **Define overload, pause, reload, and end-of-export behavior.** These are observable product features and need deliberate rules.
6. **Make musical usefulness a release check.** Avoid shipping only a technically correct collision test. Include a scene with sensible pitch choices, collision thresholds, stereo placement, and controlled gain.
7. **Retain a small architecture.** Use typed events and direct subscriptions. Do not introduce a plugin system, a general mapping graph, or a universal event framework.

## 3. Local starting point and setup

Observed on the development machine:

- The project folder contains only the source specification; there is no application code or Git repository yet.
- Apple Silicon (`arm64`), macOS 15.1, Xcode command-line tools, and Apple Clang 16 are available.
- Git and Homebrew are available.
- `cmake`, `ninja`, `ffmpeg`, and `ffprobe` are not currently available on the command path.

Planned setup during implementation:

```sh
brew install cmake ninja ffmpeg
```

Use CMake presets for development, release, and sanitizer builds. Fetch library source using immutable release archives with checksums or exact commits. Do not track moving branches. Record actual versions, licenses, and build options in `docs/dependencies.md` after the initial integration build succeeds.

Dependencies are downloaded during setup/build, not during application use. A cached dependency checkout must support subsequent offline builds. FFmpeg remains a separately installed executable for the personal-use release.

| Component | Choice | Implementation decision |
|---|---|---|
| Application/core | C++20 | RAII, explicit ownership, bounded containers at real-time boundaries. |
| Build | CMake + Ninja | Pinned library sources; C and C++ languages enabled where dependencies require them. |
| Window/input/rendering | SDL3 | Software offscreen drawing first, texture presentation to the window. |
| Physics | Supported stable Box2D 3.x release | Engine wrapper around the actual pinned C API; no Box2D identifiers in Lua. |
| Scripting | Lua 5.4 | Deliberately retain the spec's API target and pin a patch release. |
| Playback | miniaudio low-level device API | Engine owns synthesis; miniaudio transports stereo PCM to the output device. |
| Encoding | External FFmpeg + ffprobe | Check executable and required encoder capabilities explicitly. |
| Tests | CTest | Small native test executables; no large test framework required initially. |

No exact dependency version is claimed as tested at the planning stage. Milestone 0 selects and locks the compatible set.

## 4. Engine structure and ownership

Use these build boundaries, with small internal modules rather than an interface class for every file:

| Target/module | Owns | Must not require |
|---|---|---|
| `ambiance_core` | Scene state, clock, RNG, Box2D wrapper, Lua runtime, typed events, synth, effect history | A window, audio output device, or FFmpeg process |
| `ambiance_render` | Canvas, drawing primitives, camera transform, trails and flashes | Device audio or a physical display for offscreen rendering |
| `ambiance_preview` | SDL window/events, presentation, transport commands, playback adapter, status display | An encoder |
| `ambiance_export` | Offline driver, WAV output, frame streaming, child process, progress/manifest | Real-time playback or wall-clock pacing |
| `ambiance` | CLI parsing and application startup | Engine internals exposed as public CLI state |

Primary ownership:

- `SceneSession` owns the configuration, stable object registry, physics world, Lua state, RNG, event listeners, and scheduled audiovisual commands.
- `Timeline` owns integer tick/sample indices and time conversion helpers.
- `SynthMixer` owns voices and produces PCM independently of any device.
- `RenderSnapshot` contains the immutable data needed to draw a particular presentation time.
- `PreviewTransport` owns play/pause/reset/reload state and the handoff between the engine and playback.
- `ExportJob` creates a fresh session and owns its temporary files and encoder process.

Preview has three execution contexts:

1. **Main thread:** SDL events, rendering, presentation, and status.
2. **Engine worker:** fixed physics steps, Lua, event dispatch, synth rendering, and publication of bounded snapshots/PCM.
3. **Device callback:** copy already-produced PCM from a preallocated single-producer/single-consumer ring and update atomic counters. No Lua, physics, file access, logging, allocation, or blocking locks.

This uses a deliberately simpler callback than the source specification's callback-side mixer. The shared engine-owned mixer still produces all preview and offline audio. Queues and snapshot buffers have fixed capacities and explicit overflow behavior.

Offline export runs the same core and drawing code synchronously. It never initializes an output audio device.

## 5. Timing, determinism, and transport contracts

### Shared timeline

- Physics advances at 120 ticks per simulation second by default, with a fixed, pinned Box2D substep configuration.
- Internal audio runs at 48,000 stereo sample frames per second. A sample frame contains one sample for each channel.
- At those defaults, a physics tick spans exactly 400 audio sample frames. Use integer indices rather than repeatedly adding floating-point seconds.
- An impact detected during tick `n` is timestamped at that tick's end. All callbacks and effect scheduling use that timestamp.
- Events at a sample boundary are applied before generating that sample. Equal-time events use a stable sequence number.
- Video frame `k` is sampled at rational time `k / fps`. Derive indices with integer/rational arithmetic; do not accumulate frame durations in a floating-point loop.
- Start with constant 24, 30, and 60 FPS exports. Add other rates only with their own timestamp/count tests.

Advance physics far enough to obtain bracketing states for a requested visual time. Interpolate positions for drawing, but display flashes, trail points, and other events only once their timestamps have been reached. Future events discovered by lookahead must not appear early.

### Deterministic update order

Each tick follows the same order:

1. Apply commands scheduled for this tick.
2. Run the fixed-step script update hook, if enabled.
3. Advance physics.
4. Copy contact data into engine-owned events before Box2D data becomes invalid.
5. Normalize object order and dispatch collision events in stable order.
6. Run Lua listeners in registration order; enqueue any world mutations for the next tick.
7. Record trail/effect state and publish the next snapshot.
8. Generate audio only after all events affecting that audio interval are known.

Use stable engine IDs allocated in creation order. Keep normal direction consistent if collision pair ordering is normalized. Never rely on unordered-map iteration to determine execution order.

All scene randomness comes from an explicitly specified engine RNG, including its integer-to-float conversion. Expose `scene.random()` and remove or redirect Lua's independent random functions. Do not seed from time. `scene.configure` must precede object creation and random draws; CLI seed overrides take effect before initialization and cannot be overwritten by the script.

Do not compile deterministic paths with fast-math. Record the engine, dependency, compiler/build, script hash, seed, timestep, and output settings. Guarantee repeatability for the same supported build/platform; broader cross-platform equivalence is a separate test goal. Box2D's own determinism does not establish determinism for Lua, synthesis, or rendering.

### Preview synchronization

The worker runs a small bounded distance ahead of presentation, initially targeting approximately 100 ms. This gives it time to discover collision events and render their audio before playback needs them.

Use audio consumption as the preview's pacing reference while sound is enabled. Track scene PCM consumption separately from device frames filled with silence. Account for backend buffering/latency when selecting the visual presentation time; do not mistake callback completion for sound reaching the speakers.

Milestone 2 must measure the actual latency information available from the selected backend. Document the estimate and any calibration offset. Target audiovisual alignment within one 30 FPS video frame on the tested built-in/wired device, with no accumulating drift. Wireless-device latency is a separately measured limitation, not a sample-accurate preview promise.

When audio is explicitly disabled, use a monotonic wall-clock presentation driver over the same fixed ticks. Wall time controls pacing only, never simulation outcomes.

### Pause, reset, and reload

- **Pause:** stop consuming scene PCM at a defined block boundary and stop the worker. Retain queued PCM, pending events, voice state, and lookahead snapshots. Freeze visual presentation at the matching boundary after already-submitted device audio drains. A short device-buffer response delay is expected and measured.
- **Resume:** continue the preserved timeline and buffered PCM. Do not restart notes or advance time through the pause.
- **Restart (`R`):** rebuild from the last successfully loaded script contents, seed, and effective configuration. Clear voices, effects, queues, and counters using a new session generation.
- **Reload (`L`):** read the script on disk into a candidate session. Validate it before swapping. Success restarts at zero; failure keeps the last good session available and shows the error. This is not state-preserving hot reload.
- **End:** the preview stops at the scene duration. Automatic repeat can be added as an explicit mode.

On overload, drop drawing opportunities before changing simulation behavior. An audio underrun fills the device buffer with silence, reports an error counter, and triggers a controlled rebuffer/pause; it must not silently skip simulation steps or advance the composition over missing content.

## 6. Physics, collision, and script API

### Physics contract

- Lua positions and velocities use logical scene pixels and pixels/second, with positive Y downward.
- Convert through one documented scale, initially 100 scene pixels per meter, before calling Box2D. Define density and impulse units explicitly; expose impulse in physical units.
- Support dynamic circles and axis-aligned static boxes. Rendered line boundaries may be thin static boxes initially.
- Require finite numbers, positive radii/dimensions/density, unique IDs, bounded object counts, and restitution within the documented supported range.
- Keep visual properties separate from physics fixtures.
- Use Box2D impact/hit events for sound triggers, rather than treating every touching contact as a new impact.

Collision events expose `tick`, `time`, IDs, position, normal, approach/relative normal speed, and impulse. Copy all data immediately after the step. Box2D hit events expose approach speed; they do not automatically supply the required impulse. Obtain impulse from the pinned API's contact/manifold data and document its aggregation across points/substeps. Prove that extraction in Milestone 1. If it cannot be implemented reliably, document and review an explicit event-contract change before building scenes around a different metric.

Filter impacts with a configurable minimum impulse and an optional cooldown keyed by object pair, with deterministic tie/order rules. Multiple listeners can react to one event. Persistent resting contacts must not generate repeated notes. High-speed continuous-collision event timing is subject to the pinned physics API and receives a specific test.

### Initial public Lua surface

| Function | Contract |
|---|---|
| `scene.configure(settings)` | One initial configuration call; defines logical canvas, duration, gravity, background, seed, and schema version. |
| `scene.add_circle(settings)` | Creates a validated circle and returns an opaque engine handle. |
| `scene.add_box(settings)` | Creates a validated static boundary. |
| `scene.random([min, max])` | Uses only the session's seeded RNG. |
| `scene.on_collision(callback[, filters])` | Adds a listener; returns an opaque subscription token if needed by actual examples. |
| `scene.on_update(callback)` | Fixed-tick hook, added when a concrete motion/controller example needs it. |
| `scene.get_object(id)` | Read-only object state needed by mappings; no raw physics pointers. |
| `audio.play(settings)` | Schedules a sine or bell voice at scene time. |
| `visual.flash(settings)` | Schedules a finite visual effect at scene time. |

Start with the source spec's example syntax wherever possible. Validate unknown keys, types, numeric limits, and references; include the filename, line, callback, and parameter in errors where available.

`audio.play.at` is absolute scene time and defaults to the current callback time. Reject events in an already-rendered past interval. Limit future scheduling horizon and pending event count. Notes and effects use stable ordering for equal timestamps.

`validate` loads and constructs the scene and returns a nonzero exit status on error, without opening a window or audio device. It does not promise to find callbacks that fail only later. A documented `--simulate SECONDS` mode runs a bounded offline validation interval.

### Script execution boundaries

Expose an allowlist of useful base, math, table, string, and UTF-8 functions. Omit `io`, `os`, `package`, `debug`, native module loading, file-loading helpers, bytecode input, and unrestricted dynamic loading. Engine-side loading reads the selected script file; the script itself receives no filesystem authority.

Use an allocator memory budget and instruction-count hooks for load/update/collision callbacks, plus host limits on objects, listeners, events, effect history, and string processing inputs. Prevent script-controlled protected calls/coroutines from defeating execution budgets, either by omitting those facilities initially or making exhaustion uncatchable at the host boundary. Check cancellation between bounded work units.

Contain script errors and stop the affected session with diagnostics. This embedded-language restriction is defense in depth, not an OS security boundary for arbitrary hostile code. Process isolation is a future requirement if public/untrusted scene sharing becomes a product feature.

## 7. Sound and visual design

### Sound

- One engine-owned stereo float mixer serves preview and export.
- Implement sine and layered decaying bell voices, with bounded frequency, gain, pan, duration, attack, and release.
- Exclude or attenuate bell partials above the supported Nyquist range to avoid obvious aliasing.
- Use equal-power stereo panning, a conservative master gain, and deterministic limiting/soft saturation. Document that clipping protection is not loudness normalization.
- Start with 64 voices and deterministic stealing: prefer the quietest releasing voice, then oldest voice, with stable ties. Use a short transition to avoid discontinuities when stealing.
- Keep all oscillator/envelope state across audio blocks. Different block sizes must not change the resulting PCM.
- Render silence as a valid stream of the exact requested length.
- A note outside the export interval produces no output. Notes overlapping the end are truncated with a short documented terminal fade inside the requested duration. Optional tail export comes later.

WAV is stereo 48 kHz PCM, initially 24-bit, with a documented deterministic conversion from the float mix and no random dither in the reference path. Check classic WAV size limits before starting; add RF64 or another large-file container before claiming multi-hour support.

Continuous sound follows the same timeline: start/update/stop a persistent voice at timestamped control points and smooth parameter changes in the mixer. Do not create a new oscillator every simulation tick. Promote one position- or speed-controlled drone into the first release if ambient sound is the user's priority.

### Visuals

- Render circles, static boxes, background, finite flashes, and simple fading trails into an RGBA canvas.
- Use the same drawing implementation and scene-time sampling in preview and export.
- Store trail points at fixed simulation intervals, with bounded lifetime/history. Compute opacity from sample age at presentation time; never fade according to how many frames were drawn.
- Resize/letterbox the window independently of world coordinates.
- Treat export `--width` and `--height` as output pixel dimensions. Preserve the logical scene aspect ratio with letterboxing by default. Do not alter the physics world.
- Keep status text and error overlays outside exported scene pixels.
- Define the initial color path explicitly: 8-bit canvas, conventional SDR/sRGB appearance, documented video conversion/tagging. HDR and color-managed production output are later work.

The initial CPU/offscreen path is chosen for simplicity and testability. If it misses the preview budget, optimize measured bottlenecks or introduce an accelerated backend behind the same draw data. Preserve a reference software path for deterministic export tests.

## 8. Offline export and CLI

Planned commands:

```sh
ambiance preview examples/bouncing_bells.lua
ambiance validate examples/bouncing_bells.lua --simulate 2
ambiance export examples/bouncing_bells.lua \
  --output output/bouncing_bells.mp4 \
  --duration 10 --width 1280 --height 720 --fps 30 --seed 42 \
  --keep-wav
ambiance export examples/bouncing_bells.lua \
  --output output/bouncing_bells.wav --duration 10 --seed 42
```

WAV-only export must not require FFmpeg, an SDL window, or device audio. Missing FFmpeg must not prevent validation or preview.

### Exact duration policy

Export covers `[0, duration)`. For the initial fixed-frame-rate video implementation, require `duration × fps` to be an integer and report a useful error otherwise. Parse duration as a bounded decimal/rational value, not a fuzzy floating-point frame count. Require a whole number of 48 kHz sample frames as well; ordinary whole-second durations satisfy both.

Thus a 10-second, 30 FPS export has exactly 300 frames at times `0/30` through `299/30`, and a WAV with exactly 480,000 sample frames per channel. MP4 AAC padding/priming is measured separately; decoded AAC is not compared bitwise to the lossless WAV.

### Export pipeline

1. Validate dimensions, FPS, duration, paths, overwrite policy, script, limits, and available disk space. Require positive even dimensions for the initial H.264/yuv420p profile; apply checked arithmetic to all size calculations.
2. Resolve FFmpeg once to an executable path and verify the selected video/audio encoders. Initially require a tested `libx264` and AAC-capable build; report how to install one if unavailable.
3. Snapshot script contents and effective configuration so edits during export cannot change the job.
4. First deterministic pass: simulate and stream audio into a temporary WAV, recording a bounded/logged event digest.
5. Second deterministic pass: reset the same scene/seed and stream RGB frames at exact timestamps into FFmpeg. Mux with the WAV from the first pass. Compare the event digest to detect unexpected pass divergence.
6. Encode H.264/AAC with an explicit constant frame rate, pixel format, audio rate, and web-friendly MP4 metadata placement. Use explicit interval lengths; do not rely on `-shortest` to define correctness.
7. Verify expected stream properties and actual video frame count with ffprobe. Report padding and any verification failure clearly.
8. Promote the completed temporary output to the requested destination only after success. Preserve any existing output unless an explicit `--overwrite` option was given.
9. Keep the WAV when requested, emit a small render manifest, and clean only the job's own temporary artifacts.

Two passes trade extra simulation work for a simple, bounded-memory pipeline with one video pipe. A single-pass encoder API can be considered only if profiling shows this is a practical problem.

Do not accumulate the full event log, PCM, or video in RAM. Retain only bounded queues, active voices/effects, and streaming hashes. Offline rendering needs bracketing snapshots; preview needs a fixed-capacity snapshot history sized for its lookahead and presentation-latency allowance. At 1080p/60, uncompressed RGB video would approach 22.4 GB per minute, so long exports must not depend on retaining raw frame files. Permit a small explicit diagnostic frame dump only for troubleshooting.

### Process and file handling

Spawn FFmpeg with an argument vector (`posix_spawn` on macOS behind a small platform wrapper), not shell command concatenation or `system()`. Pass output paths as literal arguments and use engine-created local temporary inputs. Support spaces and unusual characters in paths.

Drain stderr/progress while writing frames to avoid pipe deadlocks. Handle short writes, broken pipes, disk-full errors, encoder failure, cancellation, and process cleanup. Use a watchdog for a stuck encoder, with no arbitrary short total timeout on a legitimately long render. Never delete unrelated files. Preserve useful diagnostic text and optionally retain the failed job directory when requested.

## 9. Sequential implementation milestones

### Milestone 0 — Build and capability proof

Tasks:

- Create project layout, CMake presets, dependency pins, warning policy, and initial CTest registration.
- Build/link SDL3, Box2D, Lua, and miniaudio on this Mac.
- Prove an offscreen SDL software canvas works without a preview window or output audio device.
- Enumerate/test a playback device separately and inspect available latency/buffering information.
- Check FFmpeg/ffprobe and encode a tiny generated frame/audio fixture.
- Write the dependency ledger and build instructions.

Done when: clean configuration/build succeeds, the headless drawing fixture produces real pixels, the encode fixture is playable, and limitations are recorded. A successful compile alone does not pass this milestone.

### Milestone 1 — Deterministic physical scene and preview

Tasks:

- Add integer timeline, seeded RNG, stable object IDs, units, and `PhysicsWorld`.
- Build a temporary hardcoded scene with circles, floor, and walls.
- Implement collision copying, ordering, actual impulse extraction, and cooldown policy.
- Draw scene snapshots in an SDL window; add resize, pause, restart, and basic status.
- Produce a repeatable state/event digest without creating a window.

Done when: circles bounce visibly, pause/restart works, and two runs of a fixed-seed scenario produce matching state/event results. The collision contract is verified against the pinned Box2D API.

### Milestone 2 — Shared synthesis and synchronization proof

Tasks:

- Implement sine/bell mixer, envelopes, panning, limiting, polyphony, and timestamp scheduling.
- Render deterministic PCM/WAV without a device.
- Add the worker/PCM ring/device adapter and audio-paced visual presentation.
- Connect collision events to sound using temporary native handlers.
- Define pause/resume, rebuffer, reset-generation, and audio-device failure behavior.
- Export a short native fixture using the early frame/audio pipeline.

Done when: impacts can be seen and heard, the same events produce matching offline PCM across block sizes, pause/resume retains notes correctly, and a short exported fixture shows the expected audiovisual alignment without drift. Measure wired/built-in preview latency.

### Milestone 3 — Lua authoring and reliable reload

Tasks:

- Add restricted Lua environment, resource budgets, schema checking, and traceback diagnostics.
- Implement configuration, objects, seeded randomness, collision listeners, note scheduling, and flashes.
- Add `validate`, CLI override precedence, and candidate-session reload.
- Replace the temporary scene with `examples/bouncing_bells.lua` using the documented public API.
- Write the first scripting reference and small error examples.

Done when: changing Lua changes the preview and sound without rebuilding; restart reproduces it; a bad reload leaves the last good scene available; malformed/runaway scripts produce bounded, useful failures.

### Milestone 4 — Production CLI export

Tasks:

- Finish duration/FPS/resolution/seed options, WAV-only output, MP4 streaming, render manifest, and progress.
- Implement exact frame/sample counts, output validation, temporary files, safe overwrite, cancellation, and encoder cleanup.
- Test silent scenes, missing encoders, bad output paths, early process exit, and no-audio-device operation.
- Add render reproducibility, frame-count, and synchronization integration tests.

Done when: one documented command exports the Lua scene to a verified 10-second 1280×720/30 MP4 with exactly 300 video frames and a 480,000-sample-frame-per-channel source WAV. Offline execution does not require real-time pacing, a physical display, or audio playback on the tested backend.

### Milestone 5 — Usable first release

Tasks:

- Complete simulation-time trails and collision flashes in preview and export.
- Improve status/error presentation and keyboard help; keep the UI small.
- Tune the example's pitch set, density, collision filtering, and output gain.
- Complete README, scripting examples, troubleshooting, license notices, and performance measurements.
- Run longer preview/export sessions and the clean-setup checklist.
- Package a runnable local build; add distribution packaging here only if required by the audience decision.

Done when: the user can build, edit, validate, preview, reload, and export using the documentation without editing C++; the result is creatively useful and all original required MVP features pass.

### Milestone 6 — First creative expansion

Implement one continuous drone tied to a body's position or speed, including smoothed controls, a fixed-step read API, and a second example such as `orbital_drone.lua`. Add a controlled force/procedural-motion hook only if that example requires it.

If continuous ambiance is the primary goal, this milestone becomes part of the first release, rather than an optional follow-up. After that, choose the next feature from actual use: parameter sliders, reverb/delay, musical scale helpers, camera motion, or shader-based visual quality.

At every milestone, report changed files, exact commands, automated checks, manually observed behavior, measured limitations, and the next concrete step. Do not label audio as heard or visuals as reviewed unless they were actually inspected.

## 10. Verification and acceptance

Write tests for the engine's meaningful behavioral contracts as each feature lands. Do not add tests that merely repeat configuration constants or implementation details.

| Area | Required evidence |
|---|---|
| Clock | Exact tick/sample conversion; frame timestamps; boundaries at zero/end; invalid fractional duration rejected. |
| Determinism | Fresh sessions with the same seed/build match object/event digests, raw frame hashes, and PCM; changing render FPS does not change physics/events. |
| Collision model | Stable IDs/normal orientation, measured impulse, impact cooldown, resting contacts, simultaneous contacts, and high-speed boundary behavior. |
| Audio | Known event lands on the expected sample; block-size invariance; exact silence length; bounded output; deterministic voice stealing and end fade. |
| Transport | Pause freezes composition time/voices, resume continues correctly, restart removes old audio/effects, repeated reload/reset never leaks a previous session into playback. |
| Lua | Valid example, syntax/type/unknown-key errors, forbidden APIs, NaN/infinity, excessive object/event counts, runaway loop, memory budget, and callback failure. |
| Visuals | Same scene/time/resolution produces the same canvas; trails are unaffected by preview FPS; no future flash appears during interpolation. |
| Output geometry | Changing output resolution/window size does not change world state or event digests. |
| Export | Exact frame count and WAV length; correct stream settings; silent scene; audio-only path; no physical output device or real-time pacing. |
| Failure paths | Missing FFmpeg/encoder, paths with spaces, overwrite refusal, unwritable/disk-full destination, cancellation, broken pipe, and encoder failure. |
| Resources | Buffers/queues remain bounded as duration grows; temporary files are cleaned correctly; WAV size-limit errors occur before work starts. |

Use an isolated timing fixture that creates a visible flash and a known audio pulse at the same timestamp. Check pre-encode sample/frame onset exactly, then decode the MP4 and measure alignment with codec-aware tolerance. Separately test physical impacts; do not confuse physics event quantization with encoder delay.

For supported 24/30/60 FPS, an event can first become visible on the first sampled frame at or after its timestamp. This quantization is expected. A single tick is approximately 8.33 ms; document any additional collision event delay found in the pinned physics engine.

Run deterministic tests without hardware. FFmpeg integration tests may be conditional on local capability, but at least one release check must actually run them. A skipped export test does not establish export readiness.

Performance targets, to measure rather than assume:

- Reference preview: 1280×720, 60 Hz presentation, 20–100 circles, bounded trails, ordinary polyphony on this Mac.
- Keep the worker's sustained processing time below its generated audio/tick interval; track worst-case spikes and underruns.
- Aim for 95th-percentile presentation work within a 16.7 ms budget at 60 Hz.
- Run a 10-minute preview and at least a 10-minute export endurance check before claiming stable ambient use. Extend to the user's intended duration before claiming support for it.

## 11. Documentation, layout, and delivery

Planned layout:

```text
AmbianceGenerator/
  Ambiance_Generator_Codex_Spec.md
  IMPLEMENTATION_PLAN.md
  CMakeLists.txt
  CMakePresets.json
  README.md
  cmake/
  include/ambiance/
  src/
    app/
    core/
    physics/
    scripting/
    audio/
    render/
    export/
  examples/
    bouncing_bells.lua
    orbital_drone.lua       # creative expansion
  tests/
    fixtures/
  docs/
    architecture.md
    scripting-api.md
    dependencies.md
    troubleshooting.md
```

Planned build and verification commands, after implementation creates the presets:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
./build/dev/ambiance preview examples/bouncing_bells.lua
```

README must cover setup, build, editing, all controls, validation limitations, export, output-duration rules, error recovery, FFmpeg installation, headless constraints, performance limits, and known synchronization tolerances. The scripting reference defines defaults, units, ordering, budgets, and override precedence.

For a downloadable Mac app, add an `.app` bundle, proper resources/framework linking, signing/notarization validation, explicit FFmpeg discovery outside an interactive shell PATH, and clean-machine installation checks. Review dependency notices and codec distribution terms before bundling an encoder. Keep externally installed FFmpeg as the initial distribution option unless a self-contained app is explicitly required.

Windows/Linux readiness means avoiding platform coupling in the core and process wrapper. It does not mean claiming support before CI builds and platform-specific preview/audio/export checks pass.

## 12. Main risks and decision triggers

| Risk | Response and trigger |
|---|---|
| Audio/video appear offset despite a shared logical clock | Measure backend latency with the fixture in Milestone 2; implement a documented presentation offset and distinguish device delay from event scheduling errors. |
| Box2D hit data does not directly satisfy the requested impulse field | Prove extraction during Milestone 1; do not quietly relabel approach speed as impulse. |
| Software drawing misses preview performance | Profile at target scene size; optimize history/drawing first, then add an accelerated backend while retaining reference export. |
| Lua exhausts CPU/memory | Instruction/memory/host-operation budgets and bounded queues; isolate externally shared untrusted scenes in a future process model. |
| Simple bounce sounds are not musically useful | Tune the example early; promote continuous drone and musical constraints based on the user's goal. |
| Long exports consume too much memory/disk | Stream both passes, bound all history, preflight WAV size, and run duration-based endurance checks. |
| Distribution becomes larger than a personal tool | Make packaging/platform support an explicit release workstream and test on clean machines. |
| Spec grows into an editor/DAW before the core works | Finish preview/script/export acceptance before adding editor, timeline, node graph, or plugin features. |

No calendar completion date is assigned before the integration and synchronization proofs. Milestones 0–2 will establish the real dependency, timing, and performance risks; estimate remaining work after those gates using observed progress.

## 13. Technical references checked during planning

- [Box2D simulation and determinism](https://box2d.org/documentation/md_simulation.html): post-step event handling, units, and deterministic ordering inform the physics contract. Documentation may track a newer release, so implementation must use headers/docs matching the pinned source.
- [Box2D event structures](https://box2d.org/documentation/group__events.html): verify the available hit/contact fields before defining impulse extraction.
- [SDL3 software renderer](https://wiki.libsdl.org/SDL3/SDL_CreateSoftwareRenderer): supports a surface-backed drawing context; actual headless operation is an integration gate.
- [miniaudio programming manual](https://miniaud.io/docs/manual/index.html): low-level callback playback and device lifecycle constraints inform the PCM adapter.
- [Lua 5.4 reference manual](https://www.lua.org/manual/5.4/manual.html): selective library exposure, allocator control, and instruction hooks underpin the scripting boundary.
- [FFmpeg formats documentation](https://www.ffmpeg.org/ffmpeg-formats.html): raw video inputs require explicit pixel format, size, and frame rate.
- [ffprobe documentation](https://www.ffmpeg.org/ffprobe.html): stream inspection and frame counting support export verification.

These references support the design; they are not evidence that this application has been built or tested.
