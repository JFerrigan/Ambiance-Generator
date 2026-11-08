# Lua scene API, schema 1

Scenes are UTF-8/text Lua 5.4 programs, at most 1 MiB. A scene starts with `scene.configure{...}`, creates objects and persistent drones, and registers callbacks. Object creation and listener/drone registration occur during initialization. The fixed-step callbacks read objects and schedule sound/visual changes; they do not add or modify physics bodies. Handles are integers, stable in creation order, starting at 1; names are unique strings, at most 128 bytes. State reads return copies.

Numbers must be finite and have the specified type/range; numeric strings are not accepted. Unknown settings and non-string setting keys are errors. Colors are three-element RGB arrays with channels 0..255, converted to 8-bit integers. Positive Y points downward. Positions, sizes, and velocities use logical pixels and pixels/second; gravity uses pixels/second². Physics converts 100 logical pixels to 1 meter. Density is kg/m². Collision impulse is kg·m/s.

## Configure once

```lua
scene.configure{
  schema=1, width=1280, height=720, duration=60, seed=42,
  gravity_x=0, gravity_y=240, background={12,17,29},
  min_impulse=0.01, cooldown=0.12
}
```

These are the defaults. Width/height are integer logical dimensions 64..4096. Duration is 0.001..3600 seconds. Gravity components are -5000..5000. Seed is an integer 0..2^53-1; CLI `--seed` wins before any random draw. `min_impulse` is 0..1,000,000; `cooldown` is 0..10 seconds and applies per unordered object pair. Configure must precede all other host API calls.

## Objects

```lua
local ball = scene.add_circle{
  name='ball', x=200, y=100, vx=120, vy=0, radius=15,
  density=1, restitution=0.94, friction=0,
  color={200,220,255}, trail=0.65
}
local floor = scene.add_box{
  name='floor', x=640, y=680, width=1180, height=12,
  restitution=0.94, friction=0, color={41,56,78}
}
local o = scene.get_object(ball) -- a name such as 'ball' also works
-- o.id, o.name, o.x, o.y, o.vx, o.vy, o.radius
```

Circles are dynamic; boxes are static and axis-aligned, specified by their center. Defaults: x/y/vx/vy=0, radius=15, width=100, height=10, density=1, restitution=0.94, friction=0, color as above, trail=0. Omitted names become `object_1`, etc. Coordinates are -20000..20000; velocities -10000..10000; radius 1..1000; box dimensions 1..10000; density 0.01..100; restitution/friction 0..1. Static boxes reject nonzero velocities and trails. `radius` is used by circles; `width`/`height` by boxes. Trail lifetime is 0..2 seconds, sampled every 1/30 second. At most 256 objects may be created. Body rotation is locked; continuous collision detection is enabled for circles. There is no force, teleport, spawn-after-initialization, or body removal API in this release.

## Seeded randomness

```lua
local unit = scene.random()        -- [0,1)
local speed = scene.random(-80,80) -- [min,max)
```

This is the session's SplitMix64 generator: standard 64-bit SplitMix constants, top 53 bits divided by 2^53. Bounds must be finite, ordered, and within ±1e9. All randomness in examples comes from this generator. Lua's independent `math.random` and `math.randomseed` are absent.

## Fixed updates and collision events

```lua
scene.on_update(function(time, dt)
  -- Time at the start of a fixed tick, dt=1/120.
  local o=scene.get_object('ball')
end)
scene.on_collision(function(e)
  -- e.tick, e.time, e.a, e.b, e.x, e.y,
  -- e.normal_x, e.normal_y, e.speed, e.impulse
end)
```

Register one function per call, up to 16 of each kind. Registration order determines callback order. `get_object` in an update returns the last completed physics state. A collision callback sees the newly completed state. Object IDs satisfy `a < b`; the normal points from a to b. Position is the Box2D hit point in logical pixels, speed is positive approach speed in pixels/second, impulse is the measured sum of the matched manifold points' total normal impulses across solver substeps and restitution. Impacts are sorted by a, b, x, y, then filtered by configured impulse and pair cooldown. Filter by object ID/name inside the callback.

The timestamp is the detecting tick's end, an exact multiple of 400 sample frames. Box2D speculative/continuous-contact event timing can be quantized or delayed by a tick; it is not a sub-tick impact timestamp. Resting contact is not treated as a new impact every tick. No callback receives Box2D handles or native pointers.

## Notes

```lua
audio.play{
  instrument='bell', frequency=440, gain=0.2, pan=0,
  duration=1, attack=0.005, release=0.1,
  at=0.5
}
```

Defaults are shown except `instrument='sine'` and `at=current callback time`. `at` is absolute scene seconds, rounded to the nearest 48 kHz sample frame. It must be current time through 30 seconds ahead, and never in the rendered past. Frequency 20..12000 Hz; gain 0..1; pan -1..1; duration 0.002..30 seconds; attack 0.001..5 seconds; release 0.001..10 seconds. Sounds outside the requested output interval are inaudible. Notes overlapping the end are truncated with the shared final 5 ms fade.

The stereo float mixer uses equal-power panning, a 0.45 master scale and `x/(1+abs(x))` soft limiting. Sine notes have linear attack/release. Bells add decaying partials at 1, 2.76 and 5.4 times the fundamental; partials above 22 kHz are omitted. This is clipping protection, not loudness normalization.

There are 64 concurrent voices. Persistent drones are protected from note stealing. At capacity, a note takes the quietest releasing note (gain times release envelope), otherwise the oldest note, with stable ties. A 128-sample transition carries over the stolen voice's last output to reduce discontinuities. PCM is identical across mixer block sizes in the same build.

## Persistent motion-controlled sound

```lua
local drone=audio.start{frequency=110,gain=0.18,pan=0,attack=0.5}
scene.on_update(function(time, dt)
  local o=scene.get_object('ball')
  audio.update{id=drone,frequency=110+math.abs(o.vy)*0.1,pan=o.x/640-1}
  if time>=20 and time<20+dt then audio.stop{id=drone} end
end)
```

`start` is initialization-only and returns an integer drone handle (maximum 16). It accepts note settings, uses the sine instrument, and stays active until stopped or the composition ends. `duration`/`release` do not end a persistent drone. `update` accepts the handle in `id` and any subset of `frequency`, `gain`, and `pan`, with optional `at`; omitted controls retain their previous targets. Values use the same bounds as notes. `stop{id=...,at=...}` releases the oscillator over 100 ms. Updating a stopped drone has no effect; start a new composition to recreate it.

Control points share the audio timeline. Frequency/gain/pan approach their targets each sample with coefficient 0.002 (about 10.4 ms time constant), retaining oscillator phase. The example's position-to-pan calculation assumes x stays in 0..1280; clamp mappings for other scene geometries to the API's allowed ranges. Future starts are supported, but updates/stops before the voice's scheduled start have no effect.

## Flashes

```lua
visual.flash{x=640,y=360,radius=40,duration=0.2,color={255,245,200},at=0.5}
```

Default x/y=0, radius=40, duration=0.2, color as above, at=current callback time. Coordinates ±20000; radius 1..1000; duration 0.001..2 seconds; `at` current time through two seconds ahead. Flashes expand and fade according to scene time. Future events are hidden during lookahead. At most 4096 flashes may be retained.

## Resource boundaries and diagnostics

- 16 MiB of Lua heap and 1 MiB source; 128-byte names and setting keys.
- 200,000 Lua VM instructions per load/callback; 1,000,000 total per tick; 1024 callback dispatches per tick. Hook granularity is 100 instructions. Each callback also has a 250 ms wall-time budget, checked by the hook and standard-library wrappers; a single bounded native operation can finish after that deadline.
- Wrapped math/table/string/UTF-8 library calls accept at most 1024 arguments, 65,536 bytes per string input, and 4096 array entries per table input. A shared 1 MiB work budget per initialization/tick charges input bytes, table entries (16 bytes each), and scalar arguments. This bounds C library work that VM instruction counts do not include.
- 1024 sound/flash API events per initialization or tick, 4096 pending sound commands, 4096 flashes, 256 objects, 16 listeners per kind, and 16 drones.
- Collision hit arrays are capped at 4096 and per-shape contact reads at 256. Exceeding a budget stops the session with an error.

The available standard libraries are base, math, table, a limited string library, and UTF-8. There is no io, os, package, debug, require, coroutine, load/loadfile/dofile, bytecode, pcall/xpcall, collectgarbage, print/warn, getmetatable/setmetatable, string.dump, string.rep/format, or string pattern matching. C-side pattern operations are omitted because instruction hooks cannot interrupt them. Protected calls/coroutines are omitted so scripts cannot catch budget exhaustion and continue. Useful string length, byte/char, sub, case conversion and reversal remain available, subject to the Lua allocator budget.

Errors include the scene path, phase/callback, scene time, Lua stack trace where available, and source-line context. Syntax errors are caught at loading. Runtime errors can occur beyond a successfully validated interval. Sandbox restrictions are defense in depth within one native process, not an operating-system isolation boundary for hostile scripts.
