#include "ambiance/core.hpp"
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
int main() {
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
    std::cout << "Pendulum motion, large timeline, mixed physics and Lua contracts passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
