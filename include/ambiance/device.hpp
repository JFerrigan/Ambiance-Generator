#pragma once
#include "core.hpp"
#include <atomic>
#include <memory>
namespace ambiance {
class Transport {
  struct Impl;
  std::unique_ptr<Impl> p_;

public:
  Transport(std::unique_ptr<Session>, bool noAudio, double latencyMs, bool endless = false);
  ~Transport();
  void toggle();
  void rebuffer();
  bool playing() const;
  bool finished() const;
  uint64_t sample() const;
  uint64_t underruns() const;
  double latencyMs() const;
  double worstTickMs() const;
  std::string error() const;
  Snapshot snapshot(uint64_t sample) const;
};
} // namespace ambiance
