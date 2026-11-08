# Dependency ledger

The application pins the following source archives, includes them in `vendor/`, and verifies SHA-256 during CMake configuration. No runtime downloads occur. CMake expands them into the build directory; a clean build also works offline with these archives. System Homebrew SDL libraries are not used for application rendering.

| Component | Version | Source | SHA-256 | License / integration |
|---|---|---|---|---|
| SDL | 3.2.10 | [official archive](https://www.libsdl.org/release/SDL3-3.2.10.tar.gz) | `f87be7b4dec66db4098e9c167b2aa34e2ca10aeb5443bdde95ae03185ed513e0` | zlib; static library, software surface + native window/texture |
| Box2D | 3.1.1 | [release archive](https://codeload.github.com/erincatto/box2d/tar.gz/refs/tags/v3.1.1) | `fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4` | MIT; four substeps, single worker, no samples/unit tests |
| Lua | 5.4.8 | [official archive](https://www.lua.org/ftp/lua-5.4.8.tar.gz) | `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae` | MIT; C sources except standalone lua/luac, selective library opening |
| miniaudio | 0.11.22 | [release archive](https://codeload.github.com/mackron/miniaudio/tar.gz/refs/tags/0.11.22) | `bcb07bfb27e6fa94d34da73ba2d5642d4940b208ec2a660dbf4e52e6b7cd492f` | MIT/public-domain choice; low-level device API compiled in `device.cpp` |

Original license texts are retained in [licenses/](licenses/) and the source archives. SDL's HIDAPI and YUV support notices are also included. The miniaudio header disables decoding, encoding, resource management, nodes, engine, and generators; the application owns its mixer and WAV writer. Apple system frameworks provide the native window, audio and device infrastructure.

Build tool versions installed during verification: CMake 4.4.5, Ninja 1.13.2, Apple Clang 16.0.0, macOS 15.1 arm64. C++20, warnings enabled, no fast-math; Box2D additionally disables floating-point contraction. Dev uses RelWithDebInfo, Release uses optimization, sanitizer uses Debug with AddressSanitizer and UndefinedBehaviorSanitizer on application modules/tests. The third-party C archives are not instrumented by this application's sanitizer option.

FFmpeg/ffprobe 9.0.2 were installed separately through Homebrew. The tested encoder build exposes libx264 and AAC. The application verifies both before video rendering. The encoder is not bundled or linked into the application. Its version is recorded in MP4 manifests. Changing an external encoder can change compressed bytes, so repeatability compares raw RGBA and source PCM, not the MP4 container bytes. Codec distribution/signing/notarization work is outside this local release.
