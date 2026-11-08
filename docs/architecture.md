# Architecture and timing

`ambiance_core` (`core.cpp`, `audio.cpp`) owns Lua, Box2D, stable object IDs, SplitMix64 RNG, the integer sample clock, sound commands, synth voices, and bounded visual history. It has no SDL/device/encoder dependency. `Session::block` advances one physics tick and generates 400 stereo PCM frames. `advanceTo` discards those same blocks when only visual state is needed.

`ambiance_render` owns an SDL surface-backed software canvas. Both preview and export call `Canvas::draw`. Circle/box positions interpolate between bracketing ticks; analytic pendulums evaluate position directly at the requested integer sample, with cables and pivot markers drawn behind weights; trail points and flashes are gated by their timestamps. Trails are sampled every four ticks (30 Hz), with up to two seconds of lifetime. Render frequency does not change history. The canvas clips to the logical scene, scales uniformly, and letterboxes.

`ambiance_preview` owns the SDL window/main loop and `Transport`. Its worker runs physics, Lua, synthesis, and snapshot publication about 100 ms ahead. Its low-level miniaudio callback copies pre-generated float PCM from a preallocated single-producer/single-consumer ring, records consumption/timestamp counters, and fills unavailable output with zero. It does not run scripts, synthesize, allocate, log, or take locks. The shared `PlaybackBuffer` contains 16,384 stereo frames. Its starvation/pause/wrap/final-block behavior is exercised directly by transport tests. Sixty-four immutable snapshots cover the presentation lookback; no full composition is retained. Snapshot copies use a mutex between worker and UI, outside the audio callback.

The physical audio backend uses 48 kHz stereo floats, a requested 256-frame period and two periods. CoreAudio's nominal rate, output latency, and safety offset are queried. Display time follows submitted scene PCM minus estimated output latency. Pausing preserves the ring, pending commands, voices, and snapshots. Already submitted device audio drains; visual time catches up to that boundary and freezes. Resume holds display time until audible playback catches up, preventing timeline regression. Restart/reload destroys the old transport/device, validates a candidate separately, clears old audio, and begins a fresh session.

`preview --endless` keeps the same worker, audio ring, session, and clock running beyond configured duration. It bypasses terminal completion and fade without resetting the timeline at cycle boundaries. Restart/reload carry the option into the fresh transport; finite preview remains the default. Export retains finite duration validation and its fade.

A device notification reports interruption, stop, or route change as a visible session error. An underrun counts only real scene frames consumed, pauses, and triggers worker rebuffering. Device silence does not advance composition time. Drawing opportunities can be missed without changing simulation ticks.

`ambiance_export` owns exact decimal duration validation, streaming WAV, child processes, two deterministic passes, output verification, cancellation, and file promotion. FFmpeg is launched with `posix_spawn` and literal arguments. Stderr/stdout go to a private job log file, so pipe writes cannot deadlock behind undrained stderr. Nonblocking video writes poll with cancellation checks and a 60-second no-progress watchdog. After closing input, completion/probing have a 60-second watchdog. A job has no fixed total render timeout.

Audio pass: fresh source snapshot → fixed ticks → mixer → terminal fade → PCM24 WAV. Video pass: identical source/seed → same ticks/mixer → shared software canvas → RGBA pipe → H.264/AAC MP4. Frame hashes cover uncompressed active pixel rows; PCM hashes cover final WAV data bytes. Event digests include ordered physical state, collision data, sound controls, and flashes. The video pass advances through the complete interval and must match the audio pass's event digest. ffprobe verifies codec, geometry, audio rate, and decoded video frame count before publication.

## Tick order and boundaries

1. Fixed-step update callbacks run at tick start, in registration order, using the last completed object state.
2. Box2D advances by 1/120 s with four substeps, then state is copied. Pendulum positions/velocities are evaluated analytically at the tick end. The body vector has a null placeholder for each pendulum, preserving object/shape IDs in mixed scenes.
3. Hit data is copied, IDs ordered, normals oriented from lower to higher ID, and impacts sorted by pair and position.
4. Impulse/cooldown filters run. Collision callbacks run at the tick's end timestamp, in registration order.
5. Trail/flash history is pruned and sampled.
6. Mixer produces the interval from this tick's start up to, excluding, its end. An end-of-tick collision note starts in the next audio block, exactly at its timestamp.

Events apply before their target sample. Equal-time sound commands keep insertion order. Visual lookahead never displays future flashes/trails. Frame zero is bracketed by simulating the first tick, so a time-zero update flash appears with time-zero sound. Offline frame times are exact integer sample indices for 24/30/60 FPS.

Collision impulse is the sum of each matching contact manifold point's `totalNormalImpulse`, queried immediately after stepping. In Box2D 3.1.1 this includes substeps and restitution, in kg·m/s. Approach speed and scene positions are converted from meters to logical pixels using 100 pixels/meter. Hit events may use speculative contacts; timestamps represent the detecting tick, not an analytically solved sub-tick time. CCD can report impacts a later tick, up to the pinned engine's event-generation behavior. The high-speed fixture tests a 10,000 pixel/s circle against a thin wall without tunneling.

No fast-math is enabled. Box2D disables floating-point contraction. Determinism is scoped to the same arm64 build; compiler/library/platform changes require new reference checks. Runtime contains no network access.
