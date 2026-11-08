#include "ambiance/device.hpp"
#include "ambiance/playback_buffer.hpp"
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include <CoreAudio/CoreAudio.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <miniaudio.h>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace ambiance {
struct Transport::Impl : PlaybackBuffer {
  std::atomic<bool> stop{false};
  std::atomic<double> worstMs{0};
  mutable std::atomic<uint64_t> presented{0};
  std::unique_ptr<Session> scene;
  mutable std::mutex mutex;
  std::deque<Snapshot> snapshots;
  std::string error;
  std::thread worker;
  ma_device device{};
  std::atomic<bool> deviceFault{false};
  bool initialized = false, noAudio = false;
  uint64_t total = 0, latency = 0;
  static void callback(ma_device *d, void *out, const void *, ma_uint32 n) {
    static_cast<Impl *>(d->pUserData)->consume(static_cast<float *>(out), n);
  }
  static void notification(const ma_device_notification *notice) {
    auto *self = static_cast<Impl *>(notice->pDevice->pUserData);
    if (!self->stop.load() && (notice->type == ma_device_notification_type_stopped ||
                               notice->type == ma_device_notification_type_rerouted ||
                               notice->type == ma_device_notification_type_interruption_began)) {
      self->deviceFault.store(true);
      self->playing.store(false);
    }
  }
  Impl(std::unique_ptr<Session> s, bool silent, double extra)
      : scene(std::move(s)), noAudio(silent) {
    total = uint64_t(std::llround(scene->config().duration * 48000));
    endFrame = total;
    snapshots.push_back(scene->snapshot());
    if (!noAudio) {
      auto config = ma_device_config_init(ma_device_type_playback);
      config.playback.format = ma_format_f32;
      config.playback.channels = 2;
      config.sampleRate = 48000;
      config.periodSizeInFrames = 256;
      config.periods = 2;
      config.dataCallback = callback;
      config.notificationCallback = notification;
      config.pUserData = this;
      if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
        throw std::runtime_error(
            "Cannot open audio output. Check macOS sound settings or use --no-audio");
      initialized = true;
      // Include backend queue plus the physical CoreAudio device latency and safety offset.
      double frames =
          double(device.playback.internalPeriodSizeInFrames) * device.playback.internalPeriods;
      AudioDeviceID id = 0;
      AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice,
                                         kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
      UInt32 bytes = sizeof(id);
      if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &bytes, &id) ==
          noErr) {
        Float64 rate = 48000;
        address.mSelector = kAudioDevicePropertyNominalSampleRate;
        bytes = sizeof(rate);
        AudioObjectGetPropertyData(id, &address, 0, nullptr, &bytes, &rate);
        for (auto selector : {kAudioDevicePropertyLatency, kAudioDevicePropertySafetyOffset}) {
          UInt32 value = 0;
          address = {selector, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
          bytes = sizeof(value);
          if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &bytes, &value) == noErr)
            frames += value * 48000.0 / rate;
        }
      }
      latency = uint64_t(std::max(0.0, frames + extra * 48));
      std::cerr << "Audio: " << device.playback.name << ", backend period "
                << device.playback.internalPeriodSizeInFrames << " x "
                << device.playback.internalPeriods << ", estimated output latency "
                << latency / 48.0 << "ms\n";
    }
    try {
      worker = std::thread([this] { run(); });
      while (priming.load() && !stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (stop.load()) {
        throw std::runtime_error(getError());
      }
      if (initialized && ma_device_start(&device) != MA_SUCCESS)
        throw std::runtime_error("Cannot start audio output");
      playing.store(true);
    } catch (...) {
      stop.store(true);
      if (worker.joinable())
        worker.join();
      if (initialized) {
        ma_device_uninit(&device);
        initialized = false;
      }
      throw;
    }
  }
  ~Impl() {
    stop.store(true);
    playing.store(false);
    if (worker.joinable())
      worker.join();
    if (initialized)
      ma_device_uninit(&device);
  }
  std::string getError() const {
    std::lock_guard lock(mutex);
    return error;
  }
  void run() {
    try {
      std::array<float, 800> b{};
      std::array<float, 2048> silent{};
      auto last = std::chrono::steady_clock::now();
      double carry = 0;
      while (!stop.load()) {
        if (deviceFault.load())
          throw std::runtime_error("Audio device stopped or changed. Press R to reopen output.");
        if (noAudio) {
          auto current = std::chrono::steady_clock::now();
          if (playing.load()) {
            carry += std::chrono::duration<double>(current - last).count() * 48000;
            auto count = std::min<size_t>(1024, size_t(carry));
            if (count) {
              consume(silent.data(), count);
              carry -= count;
            }
          }
          last = current;
        }
        uint64_t w = write.load(std::memory_order_relaxed),
                 r = read.load(std::memory_order_acquire);
        if ((playing.load() || priming.load()) && w - r < 4800 && w < total) {
          auto start = std::chrono::steady_clock::now();
          scene->block(b.data());
          auto count = std::min<uint64_t>(400, total - w);
          for (uint64_t i = 0; i < count; i++) {
            float fade = float(std::min(1.0, double(total - w - i - 1) / 240));
            pcm[2 * ((w + i) % capacity)] = b[2 * i] * fade;
            pcm[2 * ((w + i) % capacity) + 1] = b[2 * i + 1] * fade;
          }
          {
            std::lock_guard lock(mutex);
            snapshots.push_back(scene->snapshot());
            while (snapshots.size() > 64)
              snapshots.pop_front();
          }
          write.store(w + count, std::memory_order_release);
          double ms =
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                  .count();
          if (ms > worstMs.load())
            worstMs.store(ms);
        } else {
          if (w >= total)
            complete.store(true, std::memory_order_release);
          if (priming.load() && (w - r >= 4800 || complete.load()))
            priming.store(false);
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    } catch (const std::exception &e) {
      {
        std::lock_guard lock(mutex);
        error = e.what();
      }
      playing.store(false);
      stop.store(true);
      priming.store(false);
    }
  }
};
Transport::Transport(std::unique_ptr<Session> s, bool noAudio, double latency)
    : p_(std::make_unique<Impl>(std::move(s), noAudio, latency)) {}
Transport::~Transport() = default;
void Transport::toggle() {
  if (!p_->getError().empty())
    return;
  p_->playing.store(!p_->playing.load());
}
void Transport::rebuffer() {
  if (p_->priming.load())
    return;
  if (p_->underruns.load() && !p_->playing.load())
    p_->playing.store(true);
}
bool Transport::playing() const {
  return p_->playing.load();
}
bool Transport::finished() const {
  return p_->complete.load() && p_->read.load() >= p_->total;
}
uint64_t Transport::sample() const {
  uint64_t r = p_->read.load(), delay = p_->latency;
  if (!p_->playing.load() || finished()) {
    uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count()),
             last = p_->lastSceneNs.load();
    uint64_t drained = (ns - last) * 48000 / 1000000000;
    delay = drained >= delay ? 0 : delay - drained;
  }
  uint64_t next = r > delay ? std::min(r - delay, p_->total) : 0;
  auto previous = p_->presented.load();
  next = std::max(previous, next);
  p_->presented.store(next);
  return next;
}
uint64_t Transport::underruns() const {
  return p_->underruns.load();
}
double Transport::latencyMs() const {
  return p_->latency / 48.0;
}
double Transport::worstTickMs() const {
  return p_->worstMs.load();
}
std::string Transport::error() const {
  return p_->getError();
}
Snapshot Transport::snapshot(uint64_t sample) const {
  std::lock_guard lock(p_->mutex);
  for (auto &s : p_->snapshots)
    if (s.tick * 400 > sample)
      return s;
  return p_->snapshots.back();
}
} // namespace ambiance
