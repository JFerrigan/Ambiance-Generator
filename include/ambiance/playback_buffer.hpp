#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
namespace ambiance {
// Shared by the real device callback and deterministic starvation/transport tests.
struct PlaybackBuffer {
  static constexpr uint64_t capacity = 16384;
  std::array<float, capacity * 2> pcm{};
  std::atomic<uint64_t> read{0}, write{0}, underruns{0}, lastSceneNs{0};
  std::atomic<bool> playing{false}, complete{false}, priming{true};
  uint64_t endFrame = std::numeric_limits<uint64_t>::max();
  void consume(float *out, size_t n) {
    std::fill_n(out, n * 2, 0.f);
    if (!playing.load(std::memory_order_acquire))
      return;
    uint64_t r = read.load(std::memory_order_relaxed), w = write.load(std::memory_order_acquire);
    uint64_t count = std::min<uint64_t>(n, w - r);
    for (uint64_t i = 0; i < count; i++) {
      out[2 * i] = pcm[2 * ((r + i) % capacity)];
      out[2 * i + 1] = pcm[2 * ((r + i) % capacity) + 1];
    }
    if (count)
      lastSceneNs.store(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count()),
                        std::memory_order_release);
    read.store(r + count, std::memory_order_release);
    if (count < n && r + count < endFrame && !complete.load(std::memory_order_acquire)) {
      underruns.fetch_add(1);
      playing.store(false, std::memory_order_release);
      priming.store(true, std::memory_order_release);
    }
  }
};
} // namespace ambiance
