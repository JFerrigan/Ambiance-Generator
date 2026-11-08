# First-release verification

Test machine: Apple Silicon arm64, 24 GiB RAM, macOS 15.1 (24B2082), Apple Clang 16. CMake 4.4.5, Ninja 1.13.2, external FFmpeg/ffprobe 9.0.2. Tests ran on 2026-11-08. Exact build/source identity, seed, dependency versions and hashes are retained in [the endurance manifest](verification/endurance-manifest.json).

## Build and milestones

The initial capability target linked the pinned dependencies and stepped a collision scene into a real offscreen SDL software surface. Independent runs matched both event and pixel digests. A real 320×180 H.264/AAC fixture and a built-in-device playback run proved encoding and audio transport before the production checks.

The device-independent core then passed physical collision, PCM, and Lua checks. The native preview opened a window and initialized MacBook Pro Speakers at 48 kHz stereo. The final keyboard smoke check injected Space, R, L and Esc into the actual SDL loop. R used cached good source despite a broken disk file; a failed L left the prior scene running until its intentional later callback error; a repaired L restarted successfully. Full runtime/source diagnostics were captured. See [native controls log](verification/preview-controls.log).

Clean Release configuration/build from the included archives succeeded, first on a temporary volume and then in the actual local `build/release` directory. No downloaded dependency checkout or output audio/display device was needed for the offline tests. `otool -L` showed only Apple/system runtime dependencies for the application binary. Its local ad-hoc code signature verified; this is not Developer ID signing/notarization.

Commands used:

```sh
cmake --preset dev
cmake --build --preset dev -j 6
ctest --preset dev --output-on-failure
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
./build/release/preview_smoke
cmake --preset sanitize -B /Volumes/AmbianceVerification/sanitize
cmake --build /Volumes/AmbianceVerification/sanitize -j 6
ctest --test-dir /Volumes/AmbianceVerification/sanitize --output-on-failure
```

Integration scratch files used `AMBIANCE_TEST_TMPDIR=/Volumes/AmbianceVerification`. The machine initially had roughly 150 MB free, and the long export correctly refused that destination. A private 1 GiB RAM volume held temporary verification builds and long outputs. It was detached after copying the logs/manifests. The local source archives, builds, and short examples remain in the project. No personal files were removed.

All three CTest suites passed in Dev, clean Release, and AddressSanitizer/UndefinedBehaviorSanitizer configurations. See [Dev](verification/dev-tests.log), [Release](verification/local-release-tests.log), and [sanitizer](verification/sanitizer-tests.log) logs. Application modules and tests are instrumented in the sanitizer preset; third-party C libraries are not.

## Behavioral evidence

| Contract | Actual check |
|---|---|
| Shared integer timing | Known sine onset: exact silence through sample frame 24,000; first nonzero oscillator sample at 24,001. Flash hidden at 23,999 and present at 24,000. |
| Deterministic composition | Independent sessions/exports match ordered event digest, PCM24 data, and raw RGBA frame hashes. Seed override changes the composition. |
| Independent drivers | Rendering at 24/30/60 FPS or a different output aspect/size leaves event/PCM results unchanged. Exports run faster than real time without playback or a display. |
| Mixer | Bit-identical PCM for block sizes 1, 127, 400 and 1024, including scheduled controls and over-64-note voice stealing. Exact silence, finite limited output, expected onset. |
| Drone | Phase retained during smoothed frequency/gain/pan changes; sample jumps bounded in the fixture. Two independent motion-drone WAVs match byte-for-byte. |
| Physics | Positive measured impulse, positive approach speed, stable IDs/normal orientation; resting-contact note suppression; a 10,000 pixel/s circle does not cross a thin wall. |
| Visuals | Repeat frame hashes; no early lookahead flash; effect expiration; trails sampled on physics time; output geometry cannot alter physics. One exported bells frame was visually inspected. |
| Pause/restart/reload | Transport counters freeze on pause and advance on resume. Device callback buffer preserves queued samples; ring wrap preserves ordering; fresh sessions restart at zero. Actual native R/L behavior checked as above. |
| Overload/end | Forced ring starvation produces silence, a counted underrun, paused consumption, and rebuffer request. Resuming consumes preserved PCM. A partial final block does not falsely report an underrun, including before worker completion publication. |
| Lua | Unknown keys, bad types/ranges, NaN, duplicates, forbidden APIs, syntax/callback errors, runaway loops, heap exhaustion, object/listener/drone/event/queue/flash budgets, large library inputs and aggregated C work limits reject with bounded errors. Long source paths retain source excerpts. |
| Files/processes | Real encoder checks, missing encoder/probe, encoder early exit/broken pipe, literal spaces/semicolon/`$(...)` in paths, invalid parent, overwrite refusal and explicit replacement, invalid durations/dimensions, cancellation with original output preserved and job cleanup. |
| Audio-only/headless | WAV export succeeds with deliberately invalid FFmpeg/ffprobe overrides; silence is exact; validation and exports initialize no device/window. |

## Encoded timing and exact counts

The isolated fixture schedules a 1000 Hz sine and a full-canvas flash at 0.5 seconds. Before encoding, the engine checks the exact sample/flash boundary. After H.264/AAC encoding and decoding, the first bright video sample was frame 15 at **0.500000 s**; the AAC pulse crossed the amplitude threshold at sample 24,002, **0.500042 s**, an offset of approximately **0.042 ms**. This verifies the encoded timeline, not an acoustic speaker measurement.

The ten-second 1280×720/30 fixture contained exactly **300 H.264 frames**, **480,000 source WAV sample frames per channel**, stereo AAC at 48 kHz, and ten-second stream durations. FFmpeg decoded 480,000 AAC frames in that fixture. The first AAC packet had PTS -1024 and `Skip Samples: 1024` priming metadata. Other durations can decode with tail padding; the lossless source WAV is the exact-count reference.

## Endurance and performance

The final 600-second bells export at 1280×720/30 completed in **161.13 seconds** while a real preview and other build/test checks ran concurrently. It contains exactly **18,000 video frames** and **28,800,000 stereo source WAV frames**. Both encoded streams report 600.000000 seconds. The source WAV has 172,800,044 bytes including its header. Independent ffprobe/WAV-header checks passed. See [probe](verification/endurance-probe.json), [manifest](verification/endurance-manifest.json), and [timing/resource log](verification/endurance-export.log).

The export's event digest was `bd244dac67abf197`, PCM24 digest `05c98b16201698f9`, and RGBA digest `0aa10bd4a90f8338`. The two simulation passes matched. Maximum resident size reported for the timed job was approximately **359.5 MiB**, including encoder activity; the application process itself was sampled near 12 MiB during streaming. Histories, PCM ring, voices, and command queues stay bounded as duration grows; no full video/event/PCM stream is retained in RAM.

The final hardware preview ran **600.004 seconds**, with **zero underruns**, **4.39483 ms 95th-percentile presentation work**, and **7.41383 ms worst worker tick** (below the 8.333 ms tick interval). Its final audio-paced scene position was 599.997 seconds, within the device-buffer response of the wall-time stop. Maximum resident size was approximately **80.7 MiB**; the OS reported a **327.3 MiB peak memory footprint** including native graphics/device resources. [Full preview timing/resource log](verification/endurance-preview.log). The export and clean builds/tests overlapped part of this run. A separate 600-second offline validation of the motion-drone example also passed ([log](verification/drone-long-validation.log)).

Preview drawing statistics cover snapshot acquisition, software drawing and texture update; they exclude the wait inside presentation/vsync. CoreAudio/backend queries reported a 256-frame period, two periods, and **13.0208 ms estimated output latency** on MacBook Pro Speakers. Actual acoustic latency has not been calibrated with an external recording device. Device playback ran successfully; no subjective listening verdict is claimed. Wireless latency is not certified.

## Delivery and remaining limits

The runnable local binary is `build/release/ambiance`. Short generated examples remain under `output/`: bells MP4 plus source WAV, and a motion-controlled drone WAV. The original plan's continuous-audio expansion is included in this release. Both Lua examples configure ten-minute preview durations and can export a shorter interval through the CLI.

This is the tested local Mac release, not a downloadable signed application or another-platform build. Objects/listeners/drone creation is initialization-only; there is no interactive editor, force API or mid-composition spawning. The interface uses small ASCII status text, with full diagnostics in the terminal. Lua remains in process; API/resource restrictions are defense in depth. High-complexity scripts can hit execution budgets or overload even below object caps. Very high-latency output can exceed the approximately 533 ms snapshot lookback. Multi-file publication is individually atomic, not a transaction. Force-kill/power loss can leave job files. Actual disk-full during a write and physical device unplug were not deliberately forced; free-space rejection, stream write-error paths, device notifications, cancellation, and deterministic starvation recovery are implemented/tested to the extent described above. No external microphone/camera latency or subjective musical assessment was performed.
