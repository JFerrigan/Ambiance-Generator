#include "ambiance/core.hpp"
#include "ambiance/render.hpp"
#include <algorithm>
#include <map>
#include <sstream>
#include <tuple>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace ambiance;
namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void rejects(const std::string &settings, const std::string &message) {
  bool rejected = false;
  try {
    Session s("scene.configure{};scene.add_pendulum{" + settings + "}", "invalid-pendulum");
  } catch (const std::exception &e) {
    rejected = std::string(e.what()).find(message) != std::string::npos;
  }
  check(rejected, ("expected rejection: " + settings).c_str());
}
} // namespace
int main(int argc, char **argv) {
  try {
    Session wave(R"(scene.configure{};for n=24,35 do
      local id=scene.add_pendulum{name='p'..n,anchor_x=640,anchor_y=80,
        length=520*(24/n)^2,radius=6,amplitude=30,cycle_duration=60,swings=n}
      local o=scene.get_object(id);assert(o.id==id and o.vx==0 and o.vy==0)
      assert(scene.get_object('p'..n).x==o.x)
    end)", "wave");
    for (auto initial : wave.snapshot().objects) {
      check(initial.vx == 0 && initial.vy == 0, "release velocity is not zero");
      const auto &p = *initial.pendulum;
      check(std::abs(std::atan2(initial.x - 640, initial.y - 80) - std::numbers::pi / 6) < 1e-14,
            "release angle differs");
      for (uint64_t sample : {0ULL, 1ULL, 12345ULL, 1440000ULL, 2879999ULL, 2880000ULL,
                             5760000ULL, 9007199254740991ULL,
                             std::numeric_limits<unsigned long long>::max() - 2880000ULL}) {
        auto o = initial;
        evaluatePendulum(o, sample);
        check(std::abs(std::hypot(o.x - 640, o.y - 80) - p.length) < 1e-10,
              "cable length changed");
        check(std::abs(std::atan2(o.x - 640, o.y - 80)) <= std::numbers::pi / 6 + 1e-14,
              "amplitude increased");
        auto repeat = o;
        evaluatePendulum(repeat, sample + p.cycleSamples);
        check(o.x == repeat.x && o.y == repeat.y && o.vx == repeat.vx && o.vy == repeat.vy,
              "cycle does not repeat exactly");
        auto reduced = initial;
        evaluatePendulum(reduced, sample % p.cycleSamples);
        check(o.x == reduced.x && o.vx == reduced.vx, "large timeline lost phase");
      }
      auto at = initial;
      evaluatePendulum(at, 123456);
      auto before = initial, after = initial;
      evaluatePendulum(before, 123455);
      evaluatePendulum(after, 123457);
      check(std::abs((after.x - before.x) * sampleRate / 2 - at.vx) < 1e-5 &&
                std::abs((after.y - before.y) * sampleRate / 2 - at.vy) < 1e-5,
            "analytic velocity differs from position derivative");
    }
    wave.advanceTo(60 * sampleRate);
    for (auto &o : wave.snapshot().objects) {
      auto initial = o;
      evaluatePendulum(initial, 0);
      check(o.x == initial.x && o.y == initial.y && o.vx == 0 && o.vy == 0,
            "session did not return to release state");
    }
    Session mixed(R"(scene.configure{gravity_y=0,min_impulse=0};local count=0
      local p=scene.add_pendulum{anchor_x=100,anchor_y=0,length=100,amplitude=0}
      local ball=scene.add_circle{x=100,y=100,vx=500,radius=10,restitution=1}
      local q=scene.add_pendulum{}
      local wall=scene.add_box{x=200,y=100,width=10,height=200,restitution=1}
      assert(p==1 and ball==2 and q==3 and wall==4)
      scene.on_collision(function(e) count=count+1;assert(e.a==ball and e.b==wall) end)
      scene.on_update(function(t)
        assert(scene.get_object(p).x==100 and scene.get_object(p).y==100)
        if t>1 then assert(count==1);assert(scene.get_object(ball).vx<0) end
      end))", "mixed");
    mixed.advanceTo(2 * sampleRate);
    rejects("swings=2.5", "integer");
    rejects("swings=0", "range");
    rejects("cycle_duration=.00101", "whole audio sample");
    rejects("cycle_duration=0", "range");
    rejects("length=0/0", "range");
    rejects("length=-1", "range");
    rejects("amplitude=181", "range");
    rejects("radius=0", "range");
    rejects("rope_color={1,2,999}", "range");
    rejects("anchor_x='bad'", "number");
    rejects("trail=1", "unknown setting");
    for (auto source : {
             "scene.configure{};scene.add_pendulum{name='x'};scene.add_circle{name='x'}",
             "scene.configure{};scene.add_box{name='x'};scene.add_pendulum{name='x'}",
             "scene.configure{};for i=1,257 do scene.add_pendulum{} end",
             "scene.configure{};scene.on_update(function()scene.add_pendulum{} end)"}) {
      bool rejected = false;
      try {
        Session s(source, "invalid");
        s.advanceTo(400);
      } catch (const std::exception &) {
        rejected = true;
      }
      check(rejected, "creation restriction was not enforced");
    }
    check(argc == 2, "source root required");
    auto source = readScript(std::string(argv[1]) + "/examples/pendulum_wave.lua");
    Session scene(source, "pendulum_wave");
    Canvas canvas(1280, 720);
    for (uint64_t sample : {0ULL, 1ULL, 12345ULL, 360000ULL, 720000ULL, 1440000ULL,
                            2879999ULL, 9007199254740991ULL}) {
      canvas.draw(scene.snapshot(), sample);
      auto hash = canvas.hash().value;
      auto changed = scene.snapshot();
      changed.tick = sample / tickSamples + 100;
      for (auto &o : changed.objects) {
        o.x = o.y = o.previousX = o.previousY = -1000;
      }
      canvas.draw(changed, sample + 60 * sampleRate);
      check(canvas.hash().value == hash, "presentation phase depends on cycle or snapshot tick");
    }
    // Independent oracle enumerates every crossing in two cycles, then orders by tick and
    // object (the callback's dispatch order). Verify all note parameters and exact samples.
    std::vector<std::tuple<uint64_t, int, uint64_t>> crossings;
    for (int n = 24; n <= 35; ++n) {
      for (uint64_t k = 0; k < uint64_t(4 * n); ++k) {
        uint64_t numerator = (2 * k + 1) * 60 * sampleRate;
        uint64_t at = numerator / (4 * n) + (numerator % (4 * n) != 0);
        crossings.emplace_back(at / tickSamples, n, at);
      }
    }
    std::sort(crossings.begin(), crossings.end());
    check(crossings.size() == 1416, "wrong oracle count");
    std::map<uint64_t, int> simultaneous;
    bool tickBoundary = false;
    std::ostringstream oracle;
    oracle << "local expected={";
    for (auto [tick, n, at] : crossings) {
      oracle << "{" << at << "," << n - 23 << "},";
      simultaneous[at]++;
      tickBoundary |= at % tickSamples == 0;
    }
    check(tickBoundary, "oracle lacks tick-boundary crossings");
    check(std::any_of(simultaneous.begin(), simultaneous.end(),
                      [](auto &v) { return v.second >= 6; }), "oracle lacks simultaneous notes");
    oracle << R"(};local count=0;local play=audio.play
      local midi={50,52,54,57,59,62,64,66,69,71,74,76}
      audio.play=function(s)
        count=count+1;local e=expected[count];assert(e,'unexpected crossing')
        assert(math.floor(s.at*48000+0.5)==e[1],'crossing timestamp/order')
        assert(s.frequency==440*2^((midi[e[2]]-69)/12),'pitch order')
        assert(s.instrument=='bell' and s.gain==0.09 and s.pan==0)
        assert(s.duration==0.65 and s.attack==0.003 and s.release==0.5)
        play(s)
      end
      )";
    oracle << source;
    oracle << R"(
      scene.on_update(function(t)
        if t>=120 then assert(count==1416,'missing/duplicated crossings') end
      end))";
    Session verified(oracle.str(), "crossing-oracle");
    verified.advanceTo(120 * sampleRate + tickSamples);
    std::cout << "1416 crossing notes verified across two cycles; repeating RGBA phases passed\n";
    std::cout << "Pendulum motion, large timeline, mixed physics and Lua contracts passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
