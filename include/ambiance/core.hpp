#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace ambiance {
constexpr int sampleRate = 48000, tickSamples = 400;
struct Color {
  uint8_t r = 200, g = 220, b = 255, a = 255;
};
struct Config {
  int width = 1280, height = 720;
  double duration = 60, gravityX = 0, gravityY = 240;
  uint64_t seed = 42;
  Color background{12, 17, 29, 255};
  double minImpulse = .01, cooldown = .12;
};
struct Object {
  int id = 0;
  std::string name;
  double x = 0, y = 0, previousX = 0, previousY = 0, vx = 0, vy = 0, radius = 0, width = 0,
         height = 0, trail = 0;
  Color color;
};
struct Trail {
  double x, y, time, radius;
  Color color;
  int id;
  double life;
};
struct Flash {
  double x, y, time, duration, radius;
  Color color;
};
struct Snapshot {
  Config config;
  uint64_t tick = 0;
  std::vector<Object> objects;
  std::deque<Trail> trails;
  std::deque<Flash> flashes;
};
struct Digest {
  uint64_t value = 14695981039346656037ULL;
  void bytes(const void *, size_t);
  template <class T> void add(const T &x) {
    bytes(&x, sizeof x);
  }
  std::string hex() const;
};
struct Sound {
  enum Kind { Note, Start, Update, Stop } kind = Note;
  uint64_t at = 0, sequence = 0;
  int id = 0;
  bool bell = false;
  double frequency = 440, gain = .2, pan = 0, duration = 1, attack = .005, release = .1;
  unsigned mask = 7;
};
class Mixer {
  struct Voice {
    bool active = false, drone = false, bell = false;
    int id = 0;
    uint64_t serial = 0, age = 0, length = 0;
    double phase = 0, f = 0, g = 0, p = 0, tf = 0, tg = 0, tp = 0, attack = 0, release = 0,
           lastL = 0, lastR = 0, tailL = 0, tailR = 0;
    int tail = 0;
  };
  std::array<Voice, 64> voices_{};
  std::vector<Sound> pending_;
  uint64_t cursor_ = 0, serial_ = 0;
  void apply(const Sound &);

public:
  void schedule(Sound);
  void render(float *stereo, size_t frames);
  uint64_t cursor() const {
    return cursor_;
  }
};
class Session {
  struct Impl;
  std::unique_ptr<Impl> p_;

public:
  Session(const std::string &source, const std::string &filename,
          std::optional<uint64_t> seed = {});
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  const Config &config() const;
  const Snapshot &snapshot() const;
  const Digest &digest() const;
  uint64_t tick() const;
  void block(float *stereo);       // advances exactly one tick, returns its 400 PCM frames
  void advanceTo(uint64_t sample); // advances whole ticks, discards PCM
};
std::string readScript(const std::string &path);
} // namespace ambiance
