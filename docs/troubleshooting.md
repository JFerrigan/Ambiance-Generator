# Troubleshooting

**Build tools missing:** install Xcode command-line tools, then `brew install cmake ninja ffmpeg`. This release supports Apple Silicon macOS. Use the pinned vendor archives, rather than changing library versions to match system packages. CMake checks archive hashes. If a build directory is damaged, remove only that build directory and configure again; keep `vendor/` for offline setup.

**Disk space:** a WAV needs 288,000 bytes/second plus a 44-byte header. MP4 export temporarily retains that WAV alongside encoded video; retaining WAV does not duplicate it during publication. Leave additional encoder/output space. A 10-minute source WAV needs 172,800,044 bytes. The application checks free space and reports write/encoder failures. Do not remove personal files to make a test pass. Verification on this machine used a disposable RAM volume because its disk was nearly full.

**No audio:** check the macOS default output and volume. Close applications holding the device exclusively. Run `preview ... --no-audio` to inspect visuals without playback. Export and validation never open an output device. Output interruptions or route changes produce an actionable error; press R to reopen the device and restart. System mute/volume cannot be detected as a playback failure.

**Audio underrun:** the preview pauses and re-buffers; press Space. Simplify scripts or scenes, close CPU-heavy applications, or reduce concurrent load. The reference has 20 circles. Limits are safety bounds, not a promise that 256 circles and maximum effects fit real-time deadlines. Physics time is never skipped to hide overload.

**Perceived audio/video offset:** the default is a CoreAudio/backend latency estimate, not an acoustic calibration. Use built-in speakers or wired output for initial checks. Add `--latency-ms 20` to delay visual presentation a further 20 ms, or a negative offset to reduce compensation (minimum total compensation is zero). Wireless output is not certified. Snapshot history is about 533 ms; very high-latency output can exceed it. Pausing has a short buffer-drain response; resuming preserves the queued composition.

**Script errors:** start with `validate FILE.lua --simulate 10`. Check the parameter and source line in the error. Configure before using host APIs; create objects/drones/listeners only during initialization. Numbers must be finite. `io`, `os`, `require`, protected calls, dynamic loads, metatables and coroutine APIs are intentionally absent. See [the scripting reference](scripting-api.md).

**Reload rejected:** the previous composition continues. Fix the disk file, then press L again. R uses the last successfully loaded source, not an unsaved edit. Validation checks only the first 100 ms before preview swapping; a later callback failure stops that new session. Read the full stack trace in the terminal if the status line is too short.

**FFmpeg missing or unsuitable:** `brew install ffmpeg`; both libx264 and AAC encoders are required. PATH and `/opt/homebrew/bin`/`/usr/local/bin` are searched. For controlled setups, `AMBIANCE_FFMPEG` and `AMBIANCE_FFPROBE` select literal executable paths. An invalid explicit override produces an error instead of falling back. WAV-only export works without either executable.

**Destination errors:** create the parent directory, use `.mp4` or `.wav`, and add `--overwrite` only when you want to replace existing regular files. The source scene may not be an output destination. A private `.ambiance-*` job directory is removed on normal errors or Ctrl-C. Force-kill/power loss cannot run cleanup; inspect any leftover job directory before removing it. The application never deletes unrelated files.

**Cancelled/stalled encoder:** Ctrl-C returns exit code 130. Broken pipes and nonzero encoder exit codes include diagnostics. The watchdog triggers after 60 seconds with no pipe progress, or 60 seconds waiting after input closes, not after 60 seconds of total rendering. Existing output is preserved until verification completes. In a failure between publishing multiple finished files, an independently published WAV/manifest may remain; the set is not transactional.

**Exact lengths:** use decimal duration, no scientific notation, at most six fractional digits. `duration*48000` and, for video, `duration*fps` must be integers. AAC includes priming/padding; the source WAV is the exact lossless reference. Color is SDR RGB converted to YUV420 with BT.709 tags; this is not an HDR/color-managed pipeline.
