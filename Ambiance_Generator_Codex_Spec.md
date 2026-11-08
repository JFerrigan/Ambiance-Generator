# Ambiance Generator — Product and Implementation Specification

## Instructions to Codex

Build an initial working version of **Ambiance Generator**, a native, code-driven audiovisual simulation tool. The core promise is simple: **a user writes a scene script that describes visual objects, physical or procedural behavior, and sound rules; the application previews that scene in real time and exports synchronized video and audio.**

Treat this document as the product specification and implementation brief. **Implement a thin, working vertical slice before expanding features.** Do not spend the first iteration building an elaborate editor, node graph, plugin architecture, or 3D renderer. If a repository already exists, inspect it before changing anything and adapt to its conventions. Otherwise, create a new CMake-based project. Favor a small, functional system over large collections of stubs.

At the end of each milestone, build and run what is available, explain what works, identify outstanding limitations, and propose the next concrete implementation step. Do not claim a feature is complete unless it actually runs.

## 1. Product vision

Ambiance Generator is a programmable environment for creating **emergent audiovisual compositions**. It combines:

- **Simulation:** physics, particles, procedural motion, time, and randomness.
- **Rendering:** visual representations of simulated objects, camera, colors, effects, and trails.
- **Audio:** real-time and offline synthesis, with sound triggered or controlled by simulation behavior.
- **Creative scripting:** simple code that defines scenes without having to modify the C++ engine.
- **Preview and export:** interactive playback for experimentation, then deterministic high-quality output.

Example: twenty glowing balls bounce inside a chamber. Impacts trigger bell notes; impact strength sets loudness; position sets stereo pan; size or vertical position sets pitch; movement produces fading trails. The scene is produced by a short script, not by animating frames or manually laying out musical notes.

**Primary user:** a developer/creative coder who wants to author a scene, immediately see and hear it, adjust a few parameters, and export a video.

**Core principle:** visuals and audio are two outputs of the **same simulated world and shared timeline**. They should never be driven by independent, drifting clocks.

## 2. Initial scope versus future scope

### MVP — required

1. Native desktop application, prioritizing macOS; CMake project that can later support Windows/Linux.
2. Load a scene from a Lua script on disk.
3. Create and simulate multiple 2D circles and static boundaries with gravity and collisions.
4. Allow visual styling: solid fill, size, color, background color, and simple fading trails.
5. Provide a real-time preview window with synchronized audio, pause, resume, restart, and basic status/error display.
6. Allow a scene script to react to collisions and schedule synthesized sounds.
7. Support at least a sine-wave and a bell-like decaying sound, with pitch, volume, pan, and duration parameters.
8. Make simulation behavior repeatable using a fixed timestep and explicit random seed.
9. Export an audio WAV and a synchronized MP4 for a selected duration and resolution using FFmpeg.
10. Include an actual example scene, documentation, and an automated test for reproducibility.

### Important next features — design for, but do not block the MVP

- Continuous property-to-audio mappings (distance → filter, speed → pitch, etc.).
- Procedural/nonphysical motion, forces, custom particle emitters, springs, orbiting objects.
- More instruments, WAV samples, filters, reverb, delay, musical scale quantization.
- A parameter inspector to adjust script-exposed variables without editing code.
- Timeline scrubbing using saved checkpoints; scene hot reload.
- Camera movement, layers, postprocessing, bloom, shaders, sprites, richer drawing primitives.
- A proper in-app source editor, presets, reusable components, and project packaging.
- Audio-reactive physics and, eventually, 3D.

### Explicit non-goals for MVP

No full video-editing timeline, DAW, general-purpose game engine, multiplayer, 3D engine, node-graph editor, visual scripting system, marketplace, or complex asset management.

## 3. Preferred technical stack

- **Language:** C++20.
- **Build:** CMake; dependencies should be reproducible/pinned (via CMake FetchContent, a package manager, or a documented setup approach).
- **Window, input, 2D rendering:** SDL3. Prefer a renderer abstraction so graphics can later move to a shader-capable backend without changing the simulation.
- **Physics:** current supported Box2D API, isolated behind an engine-specific `PhysicsWorld` interface.
- **Scene scripting:** Lua 5.4 with a small, explicit host API.
- **Audio:** miniaudio for device output and/or audio infrastructure; synth voices and event scheduling should be engine-owned and support offline rendering.
- **UI:** initially a minimal SDL window with keyboard controls; Dear ImGui is acceptable for a simple inspector once the core works.
- **Encoding:** invoke an installed FFmpeg executable from a controlled export process. Check for it and provide actionable errors if missing.
- **Tests:** CTest with focused unit/integration tests.

Codex may substitute a library if there is a concrete integration blocker, but must document why and keep all externally visible behavior below intact. Check the actual installed/API versions rather than assuming examples from older versions compile unchanged.

## 4. Architecture and responsibilities

Keep modules logically separate. These names are suggested; exact files may vary.

| Component | Responsibility |
|---|---|
| `Application` | Startup, command-line handling, preview loop, errors, shutdown. |
| `Project/Scene` | Settings, script, objects, parameters, random seed, scene lifecycle. |
| `LuaRuntime` | Load scripts, expose approved engine API, report errors with file/line. |
| `SimulationClock` | Advance fixed physics steps independent of frame rate. |
| `PhysicsWorld` | Dynamic/static bodies, forces, collision detection and contact data. |
| `EventBus` | Timestamped events such as collisions and object creation. |
| `Mapping` (later) | Connect simulation signals/events to audio and visual properties. |
| `VisualRenderer` | Draw the world for both preview and export; camera and trails. |
| `AudioEngine` | Schedule note events, synthesize/mix voices, real-time playback and WAV rendering. |
| `Exporter` | Offline simulation, frame output, WAV creation, and FFmpeg MP4 muxing. |
| `Diagnostics` | Script/runtime errors, frame timing, event counts, export progress. |

**Separation rule:** physics reports state and events; physics does not directly play sound or draw graphics. Script bindings and/or mappings decide how events affect audio and visuals.

### Runtime data flow

1. Load the scene configuration and script.
2. Initialize seeded random generator, world, objects, and callbacks.
3. Advance physics at a fixed timestep, e.g. `1/120` second.
4. Convert contacts and changes into timestamped simulation events.
5. Invoke registered script callbacks on the simulation thread; schedule audio events and visual effects.
6. Draw the world at the preview frame rate, interpolating between physics states as needed.
7. Play audio from the timestamped schedule using a stable audio clock.
8. On export, rerun from the same seed/settings and generate audio and video for the requested timeline without relying on wall-clock speed.

## 5. Scene model and scripting interface

Use Lua as the user-facing creative layer. A scene can provide:

- Canvas dimensions, background, length, random seed.
- Gravity and physical bounds.
- Objects with position, radius, velocity, mass/density, restitution, color, trail behavior, and identifiers.
- Event handlers (`on_collision`, with optional thresholds/filters).
- Audio commands (play note/synth voice at event time).
- Per-step update hooks for custom movement, if safe and cheap.
- Named parameters (later) that can appear as editable controls in the studio.

The following is the **target feel of the API**. Implement an equivalent, consistent working API and include the precise syntax in the README. Avoid exposing raw Box2D or SDL internals to Lua.

```lua
scene.configure({
  width = 1280,
  height = 720,
  duration = 30,
  seed = 42,
  background = "#080d1a",
  gravity = { x = 0, y = 450 }
})

scene.add_box({
  x = 640, y = 680,
  width = 1120, height = 20,
  static = true,
  color = "#52618c"
})

for i = 1, 20 do
  scene.add_circle({
    id = "ball_" .. i,
    x = 160 + i * 38,
    y = 70 + (i % 5) * 30,
    radius = 8 + (i % 4) * 4,
    vx = (i % 2 == 0) and 90 or -90,
    vy = 0,
    restitution = 0.9,
    color = "#7ce0ff",
    trail = true
  })
end

scene.on_collision(function(e)
  if e.impulse < 1.5 then return end
  audio.play({
    instrument = "bell",
    frequency = 220 + math.min(e.impulse * 10, 660),
    volume = math.min(e.impulse / 40, 0.8),
    pan = (e.x / 1280) * 2 - 1,
    duration = 0.6,
    at = e.time
  })
  visual.flash({ x = e.x, y = e.y, radius = 18, duration = 0.15 })
end)
```

Script requirements:

- Clear errors for unknown functions, invalid properties, and script failures.
- Bounds checking and sensible defaults for frequency, duration, pan, volume, object count, and collision handling.
- Scene start/reset produces the same starting state and sequence.
- A scene can be reloaded from disk without restarting the executable (at least via a restart command).
- No unrestricted file access or arbitrary native-function access from loaded Lua scripts by default; keep the script API explicit.
- Avoid frame-rate-dependent script behavior; use simulation time and fixed step counts.

## 6. Physics and event model

Implement dynamic circles and static boundaries/rectangles. Each simulated object needs an engine ID and human-readable script ID. Keep simulation units consistent; if pixels are used in scripts, convert predictably for Box2D internally.

The event model should include at minimum:

```text
CollisionEvent {
  time: seconds on simulation timeline
  objectAId, objectBId
  x, y: world-space contact location
  normalX, normalY
  relativeSpeed
  impulse: nonnegative collision strength
}
```

Trigger a collision response according to a clearly documented policy (e.g. impact/contact-start) rather than emitting uncontrolled duplicate sounds every physics step. Add a configurable minimum impulse and optional per-object cooldown to avoid audio spam. If precise collision timing within a physics step is unavailable, use the fixed step timestamp and document that limitation.

The event system must allow multiple listeners. One impact might trigger both a sound and a visual flash without physics knowing about either effect.

## 7. Audio subsystem

Support two paths that share the same synth/mixer logic:

**Real-time preview:** audio callback fills buffers from the event/voice queue and plays through the selected output device. Audio work must not block physics or rendering; the audio callback must avoid locks, allocation, and Lua execution.

**Offline export:** synthesize the same scheduled sounds to a stereo 48,000 Hz WAV, even if the video is exported at 30 or 60 FPS. Events carry timestamps, not merely frame indices. At export, use sample-accurate scheduling relative to those timestamps where practical.

MVP synth requirements:

- Simple sine voice and a decaying bell-like voice (can use layered oscillators).
- Frequency, gain, pan, start time, duration, and release/decay.
- Multiple simultaneous voices with a defined maximum polyphony and predictable voice-stealing behavior.
- Gain limiting/clipping protection.
- Silent output handled correctly even if no collision occurs.

Design an interface for **continuous controllers** (e.g. interpolated velocity-to-frequency modulation) even if v1 only implements event-triggered notes. Follow-on milestone should add at least one continuous drone mapped to an object's position or speed, with smoothing to avoid clicks.

## 8. Visual subsystem

MVP renderer requirements:

- Render at a user-selected canvas size independently of window size.
- Draw solid circles, static boxes/lines, and background colors.
- Draw basic fading trails that are dependent on **simulation time**, not display refresh frequency.
- Render transient collision flashes/effects from timestamped visual events.
- Resize/letterbox the preview without changing world coordinates.
- Make the same scene renderable in preview and offline export.

The engine should separate the physics shape from the displayed appearance so an object could later be visualized as a sprite, glow, trail, field, or shader effect without changing physics.

Do not require bloom, custom shaders, particle fluids, or 3D for MVP; those belong in later milestones.

## 9. Clock, determinism, and synchronization

This is foundational, not optional.

- Use fixed simulation steps, default `1/120` second, with consistent update order.
- Use a seeded RNG owned by the engine; Lua must use that RNG for scene generation if randomness is exposed.
- Preview can skip drawing frames to keep up, but must not alter the simulation's meaning or silently change the fixed timestep.
- Video frames are sampled at exact timestamps such as `frameIndex / outputFPS`.
- Audio samples are generated on a separate 48 kHz sample timeline using the same simulation event timestamps.
- Avoid using wall-clock time, OS scheduling, or render frame count to determine scene physics.
- Repeating an offline render with the same seed, resolution, duration, and engine version should produce identical raw frame data and PCM audio on the same supported platform/build. Do not assume byte-for-byte MP4 equivalence because encoder metadata can vary.
- Keep side effects during offline rendering controlled; no device playback should be needed to export.

## 10. Preview/studio UI

The first application window should prioritize usability without becoming a full IDE:

- Open/load a `.lua` scene via CLI and optionally a simple file picker.
- Show the running simulation and play its sound.
- Play/pause (Space), reset/restart (R), exit (Esc), and reload script (L or an on-screen control).
- Display scene name, time, simulation FPS/timestep, and errors.
- Provide an export action or document the CLI export command.
- If a GUI library is already convenient, add basic parameter controls and export settings. Otherwise defer the inspector until the engine works.

Editing Lua in an external IDE is acceptable for the MVP. A built-in code editor is a later feature, not a prerequisite.

## 11. Export and CLI

Provide a documented CLI along these lines (exact option spelling may vary):

```bash
ambiance preview examples/bouncing_bells.lua
ambiance validate examples/bouncing_bells.lua
ambiance export examples/bouncing_bells.lua \
  --output output/bouncing_bells.mp4 \
  --duration 30 --width 1920 --height 1080 --fps 60 --seed 42
```

Export must:

1. Validate scene and arguments and check FFmpeg availability.
2. Reset scene state to known settings and seed.
3. Run offline simulation and produce exactly the requested frame timestamps.
4. Render frames and synthesize stereo WAV aligned to the same zero time.
5. Encode/mux a playable MP4 with audio through FFmpeg.
6. Preserve the generated WAV optionally for debugging or audio-only export.
7. Report progress, errors, output path, and basic render metadata.
8. Use a bounded-memory strategy. Temporary frame files are acceptable for initial implementation; clean them up after successful export and avoid huge in-memory frame collections.

Prefer an export design that doesn't require opening a physical audio device. Make headless export possible where supported by the graphics backend. If the first iteration needs an offscreen or hidden graphics context, document that constraint.

## 12. File layout (suggestion)

```text
AmbianceGenerator/
  CMakeLists.txt
  README.md
  docs/
    architecture.md
    scripting-api.md
  src/
    main.cpp
    app/
    scene/
    scripting/
    physics/
    events/
    render/
    audio/
    export/
  include/ambiance/
  examples/
    bouncing_bells.lua
    orbital_drone.lua          # follow-on example
  tests/
  assets/                     # only if necessary
```

Prefer small interfaces, RAII resource management, clear ownership and no dependency cycles. Keep engine logic testable without needing a window or audio device.

## 13. Delivery milestones — implement sequentially

### Milestone 1: Working simulation preview

- Build system and clean app startup.
- SDL preview window.
- Box2D circles and floor/walls.
- Fixed-step simulation and seeded scene reset.
- Basic drawing and collision logging.
- One hardcoded scene is acceptable **temporarily** at this stage.

**Done when:** a native binary starts, shows bouncing circles, pauses/restarts, and repeats the same motion after reset.

### Milestone 2: Lua scenes and generated audio

- Load Lua scene settings and object creation.
- Add Lua collision callback, event bus, and basic synthesized sounds.
- Make the example scene actually follow the public scripting API.
- Add error reporting and simple validation command.

**Done when:** changing the Lua scene visibly and audibly changes the simulation without recompilation.

### Milestone 3: Offline rendering and export

- Render exact-timestamp video frames.
- Render timestamped events into stereo WAV at 48 kHz.
- Use FFmpeg to generate synchronized MP4.
- Support duration/resolution/FPS/seed overrides.
- Add reproducibility and synchronization tests.

**Done when:** a single CLI command produces a playable MP4 with matching physics visuals and impact-triggered audio.

### Milestone 4: Quality-of-life and polish

- Basic trails and collision flashes.
- Reload/preview/export controls, diagnostics, and documentation.
- Robust handling of scene failures, audio overload, no FFmpeg, and invalid export paths.
- Optional parameter inspector and one continuous-property audio mapping, if prior milestones are stable.

**Done when:** a new user can follow the README, edit the example scene, preview, and export without editing C++.

## 14. Acceptance criteria and tests

The implementation is acceptable only if all of the following work:

- A clean documented build succeeds on the development machine.
- Loading `examples/bouncing_bells.lua` creates the scene described in the script.
- The preview contains moving circles, collisions, visible styling, and audible impact notes.
- Pausing freezes simulation time and corresponding audiovisual changes; reset repeats the initial state.
- Two offline runs with matching parameters produce the same object-event log and raw audio samples (within explicitly documented float-format/platform tolerances where applicable).
- A 10-second export at 1280x720/30 FPS contains exactly 300 video frames and approximately 480,000 samples/channel at 48 kHz, subject to documented container/audio padding rules.
- Impact sounds align with observed impacts rather than drifting as render FPS changes.
- Invalid Lua syntax and missing FFmpeg result in useful error messages instead of crashes.
- Export can complete without audible real-time playback or relying on real-time pacing.
- README includes build, preview, edit, export, and troubleshooting instructions.

Suggested automated tests: seeded-state replay, collision event contract, sound scheduling at known timestamps, offline WAV sample count, scene validation, and a short smoke-test render (conditional on FFmpeg availability).

## 15. Implementation guidelines

- Avoid giant all-in-one source files and tightly coupled physics/audio/rendering code.
- Prefer functioning end-to-end features over speculative abstractions.
- Keep the public Lua API small and documented; only extend it when a real example requires it.
- Fail visibly if dependencies or capabilities are missing; do not silently output placeholder audio/video.
- Keep performance budgets practical: 20–100 circles and simple effects should preview smoothly on a typical modern desktop.
- Limit uncontrolled object spawning, event frequency, polyphony, and script execution where practical.
- Use explicit test cases for fixed-step clock and audio/video synchronization.
- Do not add cloud services, network dependencies at runtime, or telemetry.

## 16. Immediate task for Codex

**Start by implementing Milestone 1, then proceed toward Milestone 2 if the build and runtime checks succeed.** First inspect the repository, present a concise implementation plan, and begin coding. Do not attempt advanced shaders, a DAW, a complex studio layout, or 3D until the vertical slice works. After each milestone, provide:

1. Files created/modified.
2. Exact build and run commands.
3. What was actually tested and observed.
4. Known gaps and the next concrete milestone.

The intended final product is a **programmable audiovisual instrument**: simulation rules create motion and events; those same states/events drive graphics and sound; the user previews the result and reliably renders a video from it.
