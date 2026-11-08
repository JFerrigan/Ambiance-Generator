#include "ambiance/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ambiance {
void Mixer::schedule(Sound s) {
  if (s.at < cursor_)
    throw std::runtime_error("audio event is in the rendered past");
  if (pending_.size() >= 4096)
    throw std::runtime_error("pending audio event budget exceeded");
  pending_.push_back(s);
  std::stable_sort(pending_.begin(), pending_.end(), [](auto &a, auto &b) {
    return a.at < b.at || (a.at == b.at && a.sequence < b.sequence);
  });
}
void Mixer::apply(const Sound &s) {
  if (s.kind == Sound::Update || s.kind == Sound::Stop) {
    for (auto &v : voices_)
      if (v.active && v.drone && v.id == s.id) {
        if (s.kind == Sound::Stop) {
          v.tg = 0;
          v.drone = false;
          v.length = v.age + 4800;
          v.release = 4800;
        } else {
          if (s.mask & 1)
            v.tf = s.frequency;
          if (s.mask & 2)
            v.tg = s.gain;
          if (s.mask & 4)
            v.tp = s.pan;
        }
        return;
      }
    return;
  }
  auto it = std::find_if(voices_.begin(), voices_.end(), [](auto &v) { return !v.active; });
  if (it == voices_.end())
    it = std::min_element(voices_.begin(), voices_.end(), [](auto &a, auto &b) {
      if (a.drone != b.drone)
        return !a.drone;
      auto score = [](auto &v) {
        if (v.drone || v.age + v.release < v.length)
          return 2.0;
        return v.g * std::max(0.0, double(v.length) - double(v.age)) / std::max(1.0, v.release);
      };
      double x = score(a), y = score(b);
      return x == y ? a.serial < b.serial : x < y;
    });
  double l = it->lastL, r = it->lastR;
  *it = {};
  auto &v = *it;
  v.active = true;
  v.drone = s.kind == Sound::Start;
  v.bell = s.bell;
  v.id = s.id;
  v.serial = serial_++;
  v.f = v.tf = s.frequency;
  v.g = v.tg = s.gain;
  v.p = v.tp = s.pan;
  v.length = uint64_t(s.duration * sampleRate);
  v.attack = s.attack * sampleRate;
  v.release = s.release * sampleRate;
  v.tailL = l;
  v.tailR = r;
  v.tail = 128;
}
void Mixer::render(float *out, size_t frames) {
  constexpr double pi = 3.14159265358979323846;
  for (size_t n = 0; n < frames; n++, cursor_++) {
    while (!pending_.empty() && pending_.front().at <= cursor_) {
      apply(pending_.front());
      pending_.erase(pending_.begin());
    }
    double l = 0, r = 0;
    for (auto &v : voices_)
      if (v.active) {
        v.f += (v.tf - v.f) * .002;
        v.g += (v.tg - v.g) * .002;
        v.p += (v.tp - v.p) * .002;
        double env = std::min(1.0, double(v.age) / std::max(1.0, v.attack));
        if (!v.drone) {
          if (v.age >= v.length) {
            v.active = false;
            continue;
          }
          env *= std::min(1.0, double(v.length - v.age) / std::max(1.0, v.release));
        }
        double wave = std::sin(v.phase);
        if (v.bell) {
          double t = double(v.age) / sampleRate;
          wave *= std::exp(-3 * t);
          if (v.f * 2.76 < 22000)
            wave += .38 * std::sin(v.phase * 2.76) * std::exp(-6 * t);
          if (v.f * 5.4 < 22000)
            wave += .13 * std::sin(v.phase * 5.4) * std::exp(-10 * t);
        }
        double a = (v.p + 1) * pi / 4;
        double value = wave * v.g * env * .45;
        v.lastL = value * std::cos(a);
        v.lastR = value * std::sin(a);
        if (v.tail > 0) {
          v.lastL += v.tailL * v.tail / 128;
          v.lastR += v.tailR * v.tail / 128;
          --v.tail;
        }
        l += v.lastL;
        r += v.lastR;
        v.phase += 2 * pi * v.f /
                   sampleRate; // keep bell partials phase-continuous; no block-dependent wrapping
        ++v.age;
      }
    out[n * 2] = float(l / (1 + std::abs(l)));
    out[n * 2 + 1] = float(r / (1 + std::abs(r)));
  }
}
} // namespace ambiance
