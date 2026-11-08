#pragma once
#include "core.hpp"
#include <csignal>
namespace ambiance {
extern volatile std::sig_atomic_t cancelled;
struct Options {
  std::string command, script, output, duration;
  int width = 1280, height = 720, fps = 30;
  std::optional<uint64_t> seed;
  bool overwrite = false, keepWav = false, noAudio = false, endless = false;
  double simulate = 0, latencyMs = 0, previewSeconds = 0;
};
uint64_t durationSamples(const std::string &, int fps = 0);
void exportScene(const Options &);
void previewScene(const Options &);
} // namespace ambiance
