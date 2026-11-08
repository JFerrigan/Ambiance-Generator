#include "ambiance/device.hpp"
#include "ambiance/playback_buffer.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ambiance;
void wait(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
void check(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
int main() {
  try {
    PlaybackBuffer buffer;
    float out[8];
    buffer.playing.store(true);
    buffer.consume(out, 4);
    check(buffer.underruns.load() == 1 && !buffer.playing.load() && buffer.priming.load(),
          "starvation did not pause and request rebuffering");
    check(buffer.read.load() == 0 && std::all_of(out, out + 8, [](float x) { return x == 0; }),
          "device silence advanced composition");
    for (int i = 0; i < 8; i++)
      buffer.pcm[i] = float(i + 1) / 10;
    buffer.write.store(4);
    buffer.priming.store(false);
    buffer.playing.store(true);
    buffer.consume(out, 2);
    check(buffer.read.load() == 2 && out[0] == .1f && out[3] == .4f, "rebuffer skipped queued PCM");
    buffer.playing.store(false);
    buffer.consume(out, 2);
    check(buffer.read.load() == 2 && out[0] == 0, "paused device consumed preserved PCM");
    buffer.playing.store(true);
    buffer.endFrame = 4;
    buffer.consume(out, 4);
    check(buffer.read.load() == 4 && buffer.underruns.load() == 1 && out[0] == .5f && out[4] == 0,
          "partial terminal block reported a false underrun or lost PCM");
    buffer.read.store(PlaybackBuffer::capacity - 1);
    buffer.write.store(PlaybackBuffer::capacity + 1);
    buffer.endFrame = PlaybackBuffer::capacity + 1;
    buffer.pcm[(PlaybackBuffer::capacity - 1) * 2] = .7f;
    buffer.pcm[0] = .9f;
    buffer.consume(out, 2);
    check(out[0] == .7f && out[2] == .9f, "PCM ring wrap corrupted frame ordering");

    std::string source = "scene.configure{duration=3};audio.start{frequency=110,gain=.2}";
    Transport t(std::make_unique<Session>(source, "transport"), true, 0);
    wait(120);
    auto first = t.sample();
    check(first > 3000, "transport did not advance");
    t.toggle();
    wait(100);
    auto pause = t.sample();
    wait(100);
    check(t.sample() == pause, "pause advances composition");
    t.toggle();
    wait(120);
    check(t.sample() > pause + 3000, "resume did not continue");
    check(t.underruns() == 0, "silent driver underrun");
    Transport reset(std::make_unique<Session>(source, "restart"), true, 0);
    check(reset.sample() < 2000, "restart retained previous consumption");
    auto snap = reset.snapshot(0);
    check(snap.tick <= 1, "restart retained old snapshots");
    const std::string shortScene =
        "scene.configure{duration=.05};audio.start{frequency=110,gain=.2}";
    Transport finite(std::make_unique<Session>(shortScene, "finite"), true, 0);
    Transport endless(std::make_unique<Session>(shortScene, "endless"), true, 0, true);
    wait(180);
    check(finite.finished() && finite.sample() == 2400, "finite preview did not end");
    check(!endless.finished() && endless.sample() > 6000, "endless stopped at configured end");
    endless.toggle();
    wait(30);
    auto held = endless.sample();
    wait(60);
    check(endless.sample() == held, "endless pause advanced");
    endless.toggle();
    wait(100);
    check(endless.sample() > held + 3000, "endless resume failed");
    bool rejected = false;
    try {
      Session candidate("scene.configure{unknown=1}", "bad-reload");
    } catch (const std::exception &) {
      rejected = true;
    }
    wait(30);
    check(rejected && endless.sample() > held + 3000 && endless.error().empty(),
          "failed reload disturbed endless playback");
    Transport fresh(std::make_unique<Session>(shortScene, "endless-restart"), true, 0, true);
    check(fresh.sample() < 2000, "endless restart kept old clock");
    wait(100);
    check(!fresh.finished() && fresh.sample() > 2400, "restart lost endless mode");
    check(endless.underruns() == 0 && fresh.underruns() == 0, "endless silent-driver underrun");
    std::cout << "Transport pause/resume and fresh-generation restart passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
