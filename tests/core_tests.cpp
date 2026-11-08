#include "ambiance/core.hpp"
#include "ambiance/render.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ambiance;
namespace {
void expect(bool ok, const char *m) {
  if (!ok)
    throw std::runtime_error(m);
}
void invalid(const std::string &s, const std::string &part, bool step = false) {
  try {
    Session session(s, "test.lua");
    if (step)
      session.advanceTo(48000);
    throw std::runtime_error("expected script rejection: " + part);
  } catch (const std::exception &e) {
    expect(std::string(e.what()).find(part) != std::string::npos, e.what());
  }
}
std::vector<float> mix(size_t block) {
  Mixer m;
  Sound s;
  s.at = 24000;
  s.frequency = 330;
  s.gain = .4;
  s.duration = .2;
  s.bell = true;
  m.schedule(s);
  s.kind = Sound::Start;
  s.id = 1;
  s.at = 0;
  s.frequency = 110;
  s.attack = .1;
  m.schedule(s);
  s.kind = Sound::Update;
  s.at = 12000;
  s.frequency = 150;
  s.pan = .8;
  m.schedule(s);
  s.kind = Sound::Stop;
  s.at = 30000;
  m.schedule(s);
  for (int i = 0; i < 90; i++) {
    Sound n;
    n.at = uint64_t(i * 100 + 24000);
    n.frequency = 220 + i * 7;
    n.gain = .2;
    n.duration = .4;
    m.schedule(n);
  }
  std::vector<float> pcm(48000 * 2);
  for (size_t at = 0; at < 48000; at += block)
    m.render(pcm.data() + 2 * at, std::min(block, 48000 - at));
  return pcm;
}
} // namespace
int main(int argc, char **argv) {
  try {
    expect(argc == 2, "source root required");
    std::string root = argv[1];
    auto a = mix(1), b = mix(127), c = mix(400), d = mix(1024);
    expect(a == b && b == c && c == d,
           "PCM changes with audio block size / stealing / drone updates");
    for (auto f : a)
      expect(std::isfinite(f) && std::abs(f) < 1, "limiter failed");
    Mixer m;
    Sound n;
    n.at = 24000;
    n.duration = .1;
    n.attack = .001;
    m.schedule(n);
    std::vector<float> pulse(48000 * 2);
    m.render(pulse.data(), 48000);
    expect(std::all_of(pulse.begin(), pulse.begin() + 48000, [](float x) { return x == 0; }),
           "note starts early");
    expect(pulse[48002] != 0, "note onset not at timestamp plus first oscillator sample");
    Mixer silent;
    silent.render(pulse.data(), 48000);
    expect(std::all_of(pulse.begin(), pulse.end(), [](float x) { return x == 0; }),
           "silence not exact");
    Mixer smooth;
    Sound drone;
    drone.kind = Sound::Start;
    drone.id = 1;
    drone.frequency = 110;
    drone.gain = .3;
    smooth.schedule(drone);
    drone.kind = Sound::Update;
    drone.at = 24000;
    drone.frequency = 440;
    drone.gain = .5;
    drone.pan = 1;
    smooth.schedule(drone);
    smooth.render(pulse.data(), 48000);
    double jump = 0;
    for (size_t i = 2; i < pulse.size(); i++)
      jump = std::max(jump, double(std::abs(pulse[i] - pulse[i - 2])));
    expect(jump < .02, "drone changes are discontinuous");
    auto source = readScript(root + "/examples/bouncing_bells.lua");
    Session first(source, "bells"), second(source, "bells"), changed(source, "bells", 43);
    Canvas ca(320, 180), cb(320, 180);
    for (uint64_t sample = 0; sample < 10 * 48000; sample += 1600) {
      first.advanceTo(sample + 1);
      second.advanceTo(sample + 1);
      ca.draw(first.snapshot(), sample);
      cb.draw(second.snapshot(), sample);
      expect(ca.hash().value == cb.hash().value, "repeat frame hash mismatch");
    }
    first.advanceTo(480000);
    second.advanceTo(480000);
    changed.advanceTo(480000);
    expect(first.digest().value == second.digest().value, "repeat event digest mismatch");
    expect(first.digest().value != changed.digest().value, "seed override had no effect");
    for (int fps : {24, 60}) {
      Session other(source, "bells");
      Canvas size(640, 480);
      for (uint64_t f = 0; f < uint64_t(10 * fps); f++) {
        auto sample = f * 48000 / fps;
        other.advanceTo(sample + 1);
        size.draw(other.snapshot(), sample);
      }
      other.advanceTo(480000);
      expect(other.digest().value == first.digest().value, "FPS or resolution changes events");
    }
    Session contacts(R"(scene.configure{gravity_y=0,min_impulse=0,cooldown=.1};local count=0
 local ball=scene.add_circle{x=100,y=100,vx=500,radius=10,restitution=1}
 local wall=scene.add_box{x=200,y=100,width=10,height=200,restitution=1}
 scene.on_collision(function(e) count=count+1;assert(e.a==ball and e.b==wall);assert(e.impulse>0 and e.speed>400);assert(e.normal_x>0);assert(e.time==e.tick/120);end)
 scene.on_update(function(t) if t>1 then assert(count==1,'contact count');end end))",
                     "contacts");
    contacts.advanceTo(96000);
    Session resting(
        R"(scene.configure{gravity_y=240,min_impulse=.01};local count=0;scene.add_circle{x=100,y=170,radius=10,restitution=0};scene.add_box{x=100,y=200,width=200,height=10,restitution=0};scene.on_collision(function(e)count=count+1 end);scene.on_update(function(t)if t>4 then assert(count<=2 and count>0,'resting notes')end end))",
        "resting");
    resting.advanceTo(240000);
    Session fast(
        R"(scene.configure{gravity_y=0,min_impulse=0};local count=0;local ball=scene.add_circle{x=50,y=100,vx=10000,radius=5};scene.add_box{x=200,y=100,width=5,height=150};scene.on_collision(function(e)count=count+1;assert(e.impulse>0)end);scene.on_update(function(t)if t>.2 then assert(count==1);assert(scene.get_object(ball).x<200,'tunneled through wall')end end))",
        "high_speed");
    fast.advanceTo(24000);
    Session flash(
        "scene.configure{background={0,0,0}};visual.flash{x=640,y=360,radius=80,at=.5,duration=.2}",
        "flash");
    Canvas black(320, 180);
    flash.advanceTo(24001);
    black.draw(flash.snapshot(), 23999);
    auto before = black.hash().value;
    black.draw(flash.snapshot(), 24000);
    expect(before != black.hash().value, "flash onset/future gating failed");
    flash.advanceTo(48000);
    black.draw(flash.snapshot(), 48000);
    expect(before == black.hash().value, "flash failed to expire");
    invalid("scene.configure{wibble=1}", "unknown setting");
    invalid("scene.configure{width=0/0}", "width out of range");
    invalid("scene.configure{};scene.add_circle{radius=-1}", "radius out of range");
    invalid("scene.configure{};scene.configure{}", "once");
    invalid("scene.random()", "configure first");
    invalid("scene.configure{};scene.add_circle{name='a'};scene.add_circle{name='a'}",
            "duplicate object");
    invalid("scene.configure{};audio.play{at=40}", "30 seconds");
    invalid("while true do end", "instruction budget");
    invalid("scene.configure{};scene.on_update(function()while true do end end)",
            "instruction budget", true);
    invalid("scene.configure{};local a={};for i=1,257 do a[i]=scene.add_circle{} end",
            "object budget");
    invalid("scene.configure{};for i=1,1025 do audio.play{} end", "event budget");
    invalid("scene.configure{};local s='0123456789';for i=1,24 do s=s..s end", "memory");
    invalid("scene.configure{};assert(io)", "assertion failed");
    invalid("scene.configure{};assert(os)", "assertion failed");
    invalid("scene.configure{};assert(package)", "assertion failed");
    invalid("scene.configure{};assert(debug)", "assertion failed");
    invalid("scene.configure{};assert(pcall)", "assertion failed");
    invalid("scene.configure{};assert(coroutine)", "assertion failed");
    invalid("scene.configure{};scene.on_update(function()error('callback failure')end)",
            "callback failure", true);
    invalid("scene.configure{};scene.get_object(4294967297)", "unknown object");
    try {
      Session longPath(
          "scene.configure{bad_setting=1}",
          "/a/very/long/path/that/exceeds/the/short/source/path/limit/for/Lua/scene.lua");
      throw std::runtime_error("long path error was not rejected");
    } catch (const std::exception &e) {
      expect(std::string(e.what()).find("scene.configure{bad_setting=1}") != std::string::npos,
             "long source paths lost the source excerpt");
    }
    invalid("scene.configure{};local s='x';for i=1,17 do s=s..s end;string.reverse(s)",
            "string input budget");
    invalid("scene.configure{};local t={};for i=1,4097 do t[i]=i end;table.concat(t)",
            "table input budget");
    invalid("scene.configure{};local s='x';for i=1,15 do s=s..s end;for i=1,40 do "
            "string.reverse(s) end",
            "library work budget");
    invalid("scene.configure{};for i=1,17 do scene.on_update(function()end) end",
            "listener budget");
    invalid("scene.configure{};for i=1,17 do audio.start{} end", "drone budget");
    invalid(
        "scene.configure{};scene.on_update(function(t)for i=1,50 do audio.play{at=t+29}end end)",
        "pending audio event budget", true);
    invalid(
        "scene.configure{};scene.on_update(function(t)for i=1,200 do visual.flash{at=t+2}end end)",
        "flash history budget", true);
    // Failed candidate validation cannot modify the running session. Restart from cached source
    // repeats it.
    Session good(source, "saved");
    good.advanceTo(24000);
    auto saved = good.digest().value;
    invalid("scene.configure{};error('bad reload')", "bad reload");
    expect(good.digest().value == saved, "failed reload altered working scene");
    Session restart(source, "saved");
    restart.advanceTo(24000);
    expect(restart.digest().value == saved, "restart failed to repeat composition");
    std::cout << "Core contracts passed: timing, PCM, drones, polyphony, collision impulse, "
                 "cooldown, CCD, deterministic visuals/events, Lua limits, reload isolation\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
