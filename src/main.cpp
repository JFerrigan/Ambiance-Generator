#include "ambiance/app.hpp"
#include <cmath>
#include <csignal>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
namespace {
void signalHandler(int) {
  ambiance::cancelled = 1;
}
std::string help() {
  return "Ambiance Generator 1.0.0\nUsage: ambiance <preview|validate|export> scene.lua "
         "[options]\n  preview: Space pause/resume, R restart, L reload, Esc exit\n    --no-audio "
         "--latency-ms N --preview-seconds N\n  validate: --simulate SECONDS\n  export: --output "
         "FILE.mp4|FILE.wav --duration SECONDS\n    --width 1280 --height 720 --fps 24|30|60 "
         "--keep-wav --overwrite\n  all: --seed INTEGER\nDuration must contain whole audio sample "
         "frames and (for MP4) video frames.\n";
}
uint64_t integer(const std::string &s) {
  if (s.empty() || s.size() > 16)
    throw std::runtime_error("Invalid integer: " + s);
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      throw std::runtime_error("Invalid integer: " + s);
    v = v * 10 + uint64_t(c - '0');
  }
  if (v > 9007199254740991ULL)
    throw std::runtime_error("Integer exceeds 2^53-1");
  return v;
}
double real(const std::string &s, double low, double high) {
  size_t pos = 0;
  double v = std::stod(s, &pos);
  if (pos != s.size() || !std::isfinite(v) || v < low || v > high)
    throw std::runtime_error("Number out of range: " + s);
  return v;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 1 ||
        (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))) {
      std::cout << help();
      return 0;
    }
    if (argc < 3)
      throw std::runtime_error(help());
    ambiance::Options o;
    o.command = argv[1];
    o.script = argv[2];
    if (o.command != "preview" && o.command != "validate" && o.command != "export")
      throw std::runtime_error("Unknown command: " + o.command);
    std::set<std::string> seen;
    for (int i = 3; i < argc; i++) {
      std::string a = argv[i];
      if (!seen.insert(a).second)
        throw std::runtime_error("Duplicate option: " + a);
      if (a == "--no-audio" && o.command == "preview") {
        o.noAudio = true;
        continue;
      }
      if (a == "--overwrite" && o.command == "export") {
        o.overwrite = true;
        continue;
      }
      if (a == "--keep-wav" && o.command == "export") {
        o.keepWav = true;
        continue;
      }
      if (i + 1 >= argc)
        throw std::runtime_error("Missing option value: " + a);
      std::string v = argv[++i];
      if (a == "--seed")
        o.seed = integer(v);
      else if (a == "--simulate" && o.command == "validate")
        o.simulate = real(v, 0, 3600);
      else if (a == "--latency-ms" && o.command == "preview")
        o.latencyMs = real(v, -100, 250);
      else if (a == "--preview-seconds" && o.command == "preview")
        o.previewSeconds = real(v, .1, 7200);
      else if (o.command == "export") {
        if (a == "--output")
          o.output = v;
        else if (a == "--duration")
          o.duration = v;
        else if (a == "--width" || a == "--height") {
          auto n = integer(v);
          if (n > 4096)
            throw std::runtime_error("Dimensions exceed 4096");
          if (a == "--width")
            o.width = int(n);
          else
            o.height = int(n);
        } else if (a == "--fps") {
          auto n = integer(v);
          if (n != 24 && n != 30 && n != 60)
            throw std::runtime_error("FPS must be 24, 30, or 60");
          o.fps = int(n);
        } else
          throw std::runtime_error("Unknown export option: " + a);
      } else
        throw std::runtime_error("Unknown option for " + o.command + ": " + a);
    }
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);
    if (o.command == "validate") {
      ambiance::Session s(ambiance::readScript(o.script), o.script, o.seed);
      std::array<float, 800> b;
      auto samples = uint64_t(std::llround(o.simulate * 48000));
      for (uint64_t at = 0; at < samples; at += 400) {
        if (ambiance::cancelled)
          throw std::runtime_error("Validation cancelled");
        s.block(b.data());
      }
      std::cout << "Valid: " << o.script << "; seed=" << s.config().seed
                << "; objects=" << s.snapshot().objects.size() << "; ticks=" << s.tick()
                << "; digest=" << s.digest().hex() << "\n";
    } else if (o.command == "preview")
      ambiance::previewScene(o);
    else {
      if (o.output.empty())
        throw std::runtime_error("export requires --output FILE.mp4 or FILE.wav");
      ambiance::exportScene(o);
    }
    return ambiance::cancelled ? 130 : 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return ambiance::cancelled ? 130 : 1;
  }
}
