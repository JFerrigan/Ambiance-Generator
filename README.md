# Ambiance Generator

A local native instrument for this Apple Silicon Mac. Lua describes bouncing circles, collision music, motion-controlled drones, and a repeating pendulum wave. Preview with sound, edit in your text editor, reload, and export stereo WAV or H.264/AAC MP4. Runtime uses no network services or telemetry.

## Run the tested local build

From this folder:

```sh
./build/release/ambiance preview examples/bouncing_bells.lua
./build/release/ambiance preview examples/orbital_drone.lua
./build/release/ambiance preview examples/pendulum_wave.lua --endless
```

Use **Space** to pause/resume, **R** to restart the last good scene, **L** to reload the file from disk, and **Esc** to exit. Edit an example in an external editor and press L. A reload checks initialization and the first 100 ms of callbacks before replacing playback; invalid candidates leave the working composition running. Errors appear below the scene and in the terminal. A callback can still fail later in a composition.

Scene time, playback state, errors, controls, and underruns appear outside the exported image. Resizing preserves logical scene coordinates and adds letterboxing. Preview ends at the configured duration by default. `preview --endless` keeps the same engine, audio queue, and clock running past that duration, with no terminal fade. It preserves Space/R/L/Esc behavior. Exports always use a finite duration. Restart repeats the composition from its saved source and seed, even if disk edits have not been reloaded.

## Repeating pendulum wave

[pendulum_wave.lua](examples/pendulum_wave.lua) releases 12 weights from rest at 30 degrees from one pivot. The lengths are `520 * (24 / swings)^2` pixels, for 24 through 35 complete swings per 60 seconds. Each bottom crossing plays a quiet bell in ascending D-major pentatonic pitches, with shorter pendulums playing higher notes. Trails and flashes are off so the starting image repeats exactly every minute.

This is an ideal undamped model with prescribed periods. The lengths follow small-angle pendulum ratios; the motion does not simulate gravity, finite-angle period corrections, collisions, or energy loss. Geometry repeats exactly. Overlapping bell tails and the finite export's end fade mean the audio file is not a seamless loop.

Run continuously with the `--endless` command above. Export a complete minute:

```sh
mkdir -p output
./build/release/ambiance export examples/pendulum_wave.lua \
  --output output/pendulum_wave.mp4 --duration 60 \
  --width 1280 --height 720 --fps 30 --keep-wav
```

See [pendulum verification](docs/verification.md#pendulum-wave-and-endless-preview) for measured results.

## Build from the included sources

Tested on arm64 macOS 15.1 with Apple Clang 16. Install Xcode command-line tools and the command-line dependencies:

```sh
xcode-select --install  # only if the tools are absent
brew install cmake ninja ffmpeg
cmake --preset release
cmake --build --preset release -j 8
ctest --preset release --output-on-failure
# Optional: opens a native window and injects control/reload checks
./build/release/preview_smoke
```

The four library source archives are included in `vendor/` and checked against SHA-256 pins by CMake. Configuration and subsequent builds work offline; FFmpeg is a separately installed executable. Allow at least 500 MB for a build, and additional space for exports. The application reports insufficient disk space before rendering. Development and address/undefined-behavior sanitizer presets are `dev` and `sanitize`.

## Validate a scene

```sh
./build/release/ambiance validate examples/bouncing_bells.lua
./build/release/ambiance validate examples/orbital_drone.lua --simulate 10 --seed 73
```

Validation opens no display or audio device. Without `--simulate`, it checks scene initialization only. Simulation checks the selected interval, not every possible future callback. The command-line seed takes precedence over the script. All examples can be changed without rebuilding C++.

## Export

Create your destination folder first. Ten seconds at 720p/30:

```sh
mkdir -p output
./build/release/ambiance export examples/bouncing_bells.lua \
  --output output/bells.mp4 --duration 10 \
  --width 1280 --height 720 --fps 30 --seed 42 --keep-wav
```

This produces `bells.mp4`, `bells.wav`, and `bells.mp4.json`. The manifest records versions, build, seed, source hash, event/PCM/pixel digests, exact counts, and elapsed time. Existing output, manifest, or requested WAV requires `--overwrite`. Paths with spaces and shell characters are passed literally to the encoder.

Audio alone does not require FFmpeg, a window, or a physical playback device:

```sh
./build/release/ambiance export examples/orbital_drone.lua \
  --output output/drone.wav --duration 60
```

Exports run as fast as the machine permits, with bounded memory. MP4 uses two deterministic simulation passes and checks their event digests. Audio streams to a temporary WAV; video streams directly to FFmpeg. Ctrl-C cancels and removes incomplete job files. Completed artifacts replace requested paths only after encoding and verification. Each file is published atomically; a multiple-file export is not an atomic transaction.

Choose 24, 30, or 60 FPS and even dimensions from 64 to 4096. Logical scene dimensions do not change physics when output size changes. A duration is decimal seconds with up to six fractional digits, at most one hour. It must produce whole 48 kHz audio sample frames and whole video frames. For example, `10` works at all supported rates; `0.01` is invalid for video. Omit duration to use the scene's configured duration.

WAV is stereo 48 kHz 24-bit PCM, without dither. The final 5 ms fade to zero within the requested interval. A ten-second WAV contains exactly 480,000 sample frames per channel; a 30 FPS video contains exactly 300 frames. AAC has codec priming and padding; inspect source WAV counts separately from decoded AAC. The included integration test measures decoded audiovisual onset.

## Authoring and limits

Start with [bouncing_bells.lua](examples/bouncing_bells.lua) [orbital_drone.lua](examples/orbital_drone.lua), or [pendulum_wave.lua](examples/pendulum_wave.lua). The complete API, units, scheduling rules, parameter limits, and restriction policy are in [docs/scripting-api.md](docs/scripting-api.md). The drone example keeps two oscillators alive and smoothly changes pitch, gain, and stereo position from object motion.

The engine uses 120 fixed physics ticks and 48,000 audio sample frames per second. A tick is exactly 400 sample frames. Repeatability is guaranteed for the same supported build/platform, source, seed, duration, and output settings. The manifest's FNV-1a hashes detect accidental changes; they are not security signatures. Export FPS, window size, and audio callback block size do not change the composition.

An underrun inserts device silence, freezes composition consumption, and re-buffers without skipping physics. The preview reports it and pauses; press Space to continue. Script/runtime errors stop the affected session with diagnostics. R reopens a stopped/changed audio device. `--no-audio` uses wall time to pace the same engine without a device. `--latency-ms N` adds a calibration offset to the backend's latency estimate.

Preview latency is estimated from CoreAudio-reported latency/safety offset and backend buffer sizes. It has not been calibrated with an external microphone/camera loopback. Bluetooth alignment is not certified. Software visuals use conventional 8-bit SDR RGB; MP4 uses limited-range YUV420 and BT.709 tags, without a production color-management pipeline.

This personal local release has no installer, signing/notarization, embedded editor, or bundled FFmpeg. Other operating systems and sharing arbitrary hostile scripts are future work. Lua has resource and API restrictions, but runs in the application process. Use scenes you trust.

See [verification results](docs/verification.md), [architecture](docs/architecture.md), [dependency notices](docs/dependencies.md), and [troubleshooting](docs/troubleshooting.md).
