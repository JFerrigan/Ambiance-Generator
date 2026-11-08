#include "ambiance/core.hpp"
#include <box2d/box2d.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
namespace ambiance {
void evaluatePendulum(Object &o, uint64_t sample) {
  const auto &p = o.pendulum.value();
  // Reduce before converting to floating point, including at very large timeline indices.
  uint64_t phaseSample = ((sample % p.cycleSamples) * p.swings) % p.cycleSamples;
  double phase = 2 * std::numbers::pi * double(phaseSample) / double(p.cycleSamples);
  double amplitude = p.amplitude * std::numbers::pi / 180;
  double angle = amplitude * std::cos(phase);
  double angularVelocity = -amplitude * std::sin(phase) * 2 * std::numbers::pi *
                           double(p.swings) * sampleRate / double(p.cycleSamples);
  o.x = p.anchorX + p.length * std::sin(angle);
  o.y = p.anchorY + p.length * std::cos(angle);
  o.vx = p.length * std::cos(angle) * angularVelocity;
  o.vy = -p.length * std::sin(angle) * angularVelocity;
}
void Digest::bytes(const void *p, size_t n) {
  auto *b = static_cast<const unsigned char *>(p);
  while (n--) {
    value ^= *b++;
    value *= 1099511628211ULL;
  }
}
std::string Digest::hex() const {
  std::ostringstream s;
  s << std::hex << std::setw(16) << std::setfill('0') << value;
  return s.str();
}
std::string readScript(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read scene: " + path);
  f.seekg(0, std::ios::end);
  auto size = f.tellg();
  if (size < 0 || size > 1024 * 1024)
    throw std::runtime_error("Scene exceeds 1 MiB source budget");
  f.seekg(0);
  std::string s(size, '\0');
  f.read(s.data(), size);
  if (!f)
    throw std::runtime_error("Cannot read complete scene: " + path);
  return s;
}
namespace {
void fail(const std::string &m) {
  throw std::runtime_error(m);
}
void keys(lua_State *L, std::initializer_list<const char *> allowed) {
  if (!lua_istable(L, 1))
    fail("expected a settings table");
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    if (lua_type(L, -2) != LUA_TSTRING)
      fail("settings keys must be strings");
    const char *k = lua_tostring(L, -2);
    if (lua_rawlen(L, -2) > 128)
      fail("settings key exceeds 128 bytes");
    if (lua_rawlen(L, -2) != std::strlen(k))
      fail("settings keys cannot contain NUL bytes");
    bool ok = false;
    for (auto a : allowed)
      if (std::strcmp(k, a) == 0)
        ok = true;
    if (!ok)
      fail(std::string("unknown setting: ") + k);
    lua_pop(L, 1);
  }
}
double number(lua_State *L, const char *k, double def, double low, double high) {
  lua_getfield(L, 1, k);
  double v = def;
  if (!lua_isnil(L, -1)) {
    if (lua_type(L, -1) != LUA_TNUMBER)
      fail(std::string(k) + " must be a number");
    v = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  if (!std::isfinite(v) || v < low || v > high)
    fail(std::string(k) + " out of range [" + std::to_string(low) + ", " + std::to_string(high) +
         "]");
  return v;
}
std::string str(lua_State *L, const char *k, const char *def) {
  lua_getfield(L, 1, k);
  if (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TSTRING)
    fail(std::string(k) + " must be a string");
  size_t n = 0;
  const char *p = lua_isnil(L, -1) ? nullptr : lua_tolstring(L, -1, &n);
  if (n > 128)
    fail(std::string(k) + " exceeds 128 bytes");
  std::string r = p ? std::string(p, n) : def;
  lua_pop(L, 1);
  return r;
}
Color color(lua_State *L, const char *k, Color def) {
  lua_getfield(L, 1, k);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return def;
  }
  if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 3)
    fail(std::string(k) + " must be {red, green, blue}, 0..255");
  uint8_t c[3];
  for (int i = 0; i < 3; i++) {
    lua_rawgeti(L, -1, i + 1);
    if (lua_type(L, -1) != LUA_TNUMBER)
      fail("color channels must be numbers");
    double v = lua_tonumber(L, -1);
    if (!std::isfinite(v) || v < 0 || v > 255)
      fail("color channel out of range");
    c[i] = uint8_t(v);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return {c[0], c[1], c[2], 255};
}
void field(lua_State *L, const char *k, double v) {
  lua_pushnumber(L, v);
  lua_setfield(L, -2, k);
}
} // namespace
struct Session::Impl {
  lua_State *L = nullptr;
  size_t memory = 0;
  int instructions = 0, totalInstructions = 1000000, callbackCount = 0;
  size_t hostWork = 0;
  std::chrono::steady_clock::time_point deadline;
  b2WorldId world = b2_nullWorldId;
  Snapshot state;
  Mixer mixer;
  Digest digest;
  std::vector<b2BodyId> bodies;
  std::vector<int> collisionListeners, updates;
  std::map<std::pair<int, int>, uint64_t> cooldown;
  std::set<int> drones;
  uint64_t rng = 42, sequence = 0, now = 0;
  int nextDrone = 1, events = 0;
  bool configured = false, loading = true;
  std::optional<uint64_t> overrideSeed;
  std::string filename, source;
  ~Impl() {
    if (L)
      lua_close(L);
    if (B2_IS_NON_NULL(world))
      b2DestroyWorld(world);
  }
  static void *alloc(void *ud, void *p, size_t old, size_t size) {
    auto &s = *static_cast<Impl *>(ud);
    if (!p)
      old = 0;
    if (size == 0) {
      s.memory -= old;
      std::free(p);
      return nullptr;
    }
    if (size > 16 * 1024 * 1024 || s.memory - old + size > 16 * 1024 * 1024)
      return nullptr;
    void *q = std::realloc(p, size);
    if (q)
      s.memory = s.memory - old + size;
    return q;
  }
  static void hook(lua_State *L, lua_Debug *) {
    auto *s = *static_cast<Impl **>(lua_getextraspace(L));
    s->instructions -= 100;
    s->totalInstructions -= 100;
    if (s->instructions <= 0 || s->totalInstructions <= 0)
      luaL_error(L, "instruction budget exceeded (200000 per callback)");
    if (std::chrono::steady_clock::now() > s->deadline)
      luaL_error(L, "execution time budget exceeded (250ms per callback)");
  }
  // Standard-library C work is outside Lua's VM instruction hook. Bound its inputs and
  // aggregate work, and leave originals only in inaccessible closure upvalues.
  static int libraryCall(lua_State *L) {
    auto *s = static_cast<Impl *>(lua_touserdata(L, lua_upvalueindex(1)));
    int count = lua_gettop(L);
    if (count > 1024)
      return luaL_error(L, "library argument budget exceeded (1024)");
    size_t work = 8;
    for (int i = 1; i <= count; i++) {
      if (lua_type(L, i) == LUA_TSTRING) {
        size_t size = lua_rawlen(L, i);
        if (size > 65536)
          return luaL_error(L, "string input budget exceeded (65536 bytes)");
        work += size;
      } else if (lua_istable(L, i)) {
        size_t size = lua_rawlen(L, i);
        if (size > 4096)
          return luaL_error(L, "table input budget exceeded (4096 entries)");
        work += size * 16;
      } else
        work += 8;
    }
    s->hostWork += work;
    if (s->hostWork > 1024 * 1024)
      return luaL_error(L, "standard library work budget exceeded (1 MiB per tick)");
    if (std::chrono::steady_clock::now() > s->deadline)
      return luaL_error(L, "execution time budget exceeded (250ms per callback)");
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_call(L, count, LUA_MULTRET);
    if (std::chrono::steady_clock::now() > s->deadline)
      return luaL_error(L, "execution time budget exceeded (250ms per callback)");
    return lua_gettop(L);
  }
  void guardLibrary(const char *name) {
    lua_getglobal(L, name);
    int table = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, table)) {
      if (lua_iscfunction(L, -1)) {
        lua_pushvalue(L, -2);
        lua_pushlightuserdata(L, this);
        lua_pushvalue(L, -3);
        lua_pushcclosure(L, libraryCall, 2);
        lua_rawset(L, table);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  static Impl &self(lua_State *L) {
    return *static_cast<Impl *>(lua_touserdata(L, lua_upvalueindex(1)));
  }
  template <int (Impl::*F)()> static int bind(lua_State *L) {
    int result = 0;
    bool bad = false;
    try {
      result = (self(L).*F)();
    } catch (const std::exception &e) {
      lua_pushstring(L, e.what());
      bad = true;
    }
    if (bad) {
      luaL_where(L, 1);
      lua_insert(L, -2);
      lua_concat(L, 2);
      return lua_error(L);
    }
    return result;
  }
  void function(const char *name, lua_CFunction f) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, f, 1);
    lua_setfield(L, -2, name);
  }
  static int traceback(lua_State *L) {
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
  }
  void reportLuaError(const char *context) {
    const char *m = lua_tostring(L, -1);
    std::string err = filename + " [" + context + " at " +
                      std::to_string(double(now) / sampleRate) +
                      "s]: " + (m ? m : "Lua memory error");
    lua_pop(L, 1);
    std::string location = filename;
    auto pos = err.find(location + ":");
    if (pos == std::string::npos) {
      // Lua shortens long source paths in diagnostics; the session still owns the full source.
      location = filename.substr(filename.find_last_of("/\\") + 1);
      pos = err.find(location + ":");
    }
    if (pos != std::string::npos) {
      try {
        size_t line = std::stoul(err.substr(pos + location.size() + 1));
        std::istringstream lines(source);
        std::string text;
        for (size_t i = 1; i <= line && std::getline(lines, text); i++)
          if (i == line)
            err += "\n  " + text;
      } catch (...) {
      }
    }
    fail(err);
  }
  void call(int nargs, const char *context) {
    if (++callbackCount > 1024)
      fail("callback dispatch budget exceeded (1024 per tick)");
    instructions = 200000;
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    lua_sethook(L, hook, LUA_MASKCOUNT, 100);
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int rc = lua_pcall(L, nargs, 0, base);
    lua_remove(L, base);
    lua_sethook(L, nullptr, 0, 0);
    if (rc != LUA_OK)
      reportLuaError(context);
  }

  void init(const std::string &s, const std::string &f, std::optional<uint64_t> seed) {
    source = s;
    filename = f;
    overrideSeed = seed;
    if (s.size() > 1024 * 1024)
      fail("source budget exceeded");
    L = lua_newstate(alloc, this);
    if (!L)
      fail("cannot allocate Lua state");
    *static_cast<Impl **>(lua_getextraspace(L)) = this;
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);
    lua_pop(L, 1);
    for (auto name : {"dofile", "loadfile", "load", "collectgarbage", "pcall", "xpcall", "print",
                      "warn", "getmetatable", "setmetatable"}) {
      lua_pushnil(L);
      lua_setglobal(L, name);
    }
    lua_getglobal(L, "math");
    for (auto name : {"random", "randomseed"}) {
      lua_pushnil(L);
      lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    // C pattern matching/formatting have no instruction hooks. Keep the bounded, simple string
    // subset.
    lua_getglobal(L, "string");
    for (auto name : {"dump", "rep", "format", "find", "match", "gmatch", "gsub"}) {
      lua_pushnil(L);
      lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    for (auto name : {"math", "table", "string", "utf8"})
      guardLibrary(name);
    lua_newtable(L);
    function("configure", bind<&Impl::configure>);
    function("add_circle", bind<&Impl::circle>);
    function("add_box", bind<&Impl::box>);
    function("add_pendulum", bind<&Impl::pendulum>);
    function("random", bind<&Impl::random>);
    function("on_collision", bind<&Impl::listen>);
    function("on_update", bind<&Impl::update>);
    function("get_object", bind<&Impl::get>);
    lua_setglobal(L, "scene");
    lua_newtable(L);
    function("play", bind<&Impl::play>);
    function("start", bind<&Impl::start>);
    function("update", bind<&Impl::control>);
    function("stop", bind<&Impl::stop>);
    lua_setglobal(L, "audio");
    lua_newtable(L);
    function("flash", bind<&Impl::flash>);
    lua_setglobal(L, "visual");
    if (luaL_loadbufferx(L, s.data(), s.size(), ("@" + f).c_str(), "t") != LUA_OK) {
      reportLuaError("parse");
    }
    call(0, "load");
    if (!configured)
      fail("scene.configure must be called once before objects/random/audio");
    loading = false;
  }
  void require() {
    if (!configured)
      fail("call scene.configure first");
  }
  int configure() {
    if (configured || !loading)
      fail("scene.configure must be called once at initialization");
    keys(L, {"schema", "width", "height", "duration", "gravity_x", "gravity_y", "seed",
             "background", "min_impulse", "cooldown"});
    auto &c = state.config;
    if (number(L, "schema", 1, 1, 1) != 1)
      fail("unsupported schema");
    double w = number(L, "width", 1280, 64, 4096), h = number(L, "height", 720, 64, 4096);
    if (w != std::floor(w) || h != std::floor(h))
      fail("scene dimensions must be integers");
    c.width = int(w);
    c.height = int(h);
    c.duration = number(L, "duration", 60, .001, 3600);
    c.gravityX = number(L, "gravity_x", 0, -5000, 5000);
    c.gravityY = number(L, "gravity_y", 240, -5000, 5000);
    double seed = number(L, "seed", 42, 0, 9007199254740991.0);
    if (seed != std::floor(seed))
      fail("seed must be an integer");
    c.seed = overrideSeed.value_or(uint64_t(seed));
    rng = c.seed;
    c.background = color(L, "background", c.background);
    c.minImpulse = number(L, "min_impulse", .01, 0, 1000000);
    c.cooldown = number(L, "cooldown", .12, 0, 10);
    auto d = b2DefaultWorldDef();
    d.gravity = {float(c.gravityX / 100), float(c.gravityY / 100)};
    d.hitEventThreshold = .01f;
    d.restitutionThreshold = .01f;
    d.enableSleep = false;
    world = b2CreateWorld(&d);
    configured = true;
    return 0;
  }
  int add(bool isCircle) {
    require();
    if (!loading)
      fail("objects can only be created during scene initialization");
    if (state.objects.size() >= 256)
      fail("object budget exceeded (256)");
    keys(L, {"name", "x", "y", "vx", "vy", "radius", "width", "height", "density", "restitution",
             "friction", "color", "trail"});
    Object o;
    o.id = int(bodies.size() + 1);
    o.name = str(L, "name", "");
    if (o.name.empty())
      o.name = "object_" + std::to_string(o.id);
    for (auto &a : state.objects)
      if (a.name == o.name)
        fail("duplicate object name: " + o.name);
    o.x = o.previousX = number(L, "x", 0, -20000, 20000);
    o.y = o.previousY = number(L, "y", 0, -20000, 20000);
    o.vx = number(L, "vx", 0, -10000, 10000);
    o.vy = number(L, "vy", 0, -10000, 10000);
    o.color = color(L, "color", {});
    o.trail = number(L, "trail", 0, 0, 2);
    if (isCircle)
      o.radius = number(L, "radius", 15, 1, 1000);
    else {
      o.width = number(L, "width", 100, 1, 10000);
      o.height = number(L, "height", 10, 1, 10000);
      if (o.vx || o.vy || o.trail)
        fail("static boxes cannot have velocity or trails");
    }
    auto bd = b2DefaultBodyDef();
    bd.type = isCircle ? b2_dynamicBody : b2_staticBody;
    bd.position = {float(o.x / 100), float(o.y / 100)};
    bd.linearVelocity = {float(o.vx / 100), float(o.vy / 100)};
    bd.isBullet = isCircle;
    bd.fixedRotation = true;
    auto sd = b2DefaultShapeDef();
    sd.density = float(number(L, "density", 1, .01, 100));
    sd.material.restitution = float(number(L, "restitution", .94, 0, 1));
    sd.material.friction = float(number(L, "friction", 0, 0, 1));
    sd.enableHitEvents = true;
    sd.userData = reinterpret_cast<void *>(uintptr_t(o.id));
    auto body = b2CreateBody(world, &bd);
    if (isCircle) {
      b2Circle c{{0, 0}, float(o.radius / 100)};
      b2CreateCircleShape(body, &sd, &c);
    } else {
      auto poly = b2MakeBox(float(o.width / 200), float(o.height / 200));
      b2CreatePolygonShape(body, &sd, &poly);
    }
    bodies.push_back(body);
    state.objects.push_back(o);
    lua_pushinteger(L, o.id);
    return 1;
  }
  int circle() {
    return add(true);
  }
  int box() {
    return add(false);
  }
  int pendulum() {
    require();
    if (!loading)
      fail("objects can only be created during scene initialization");
    if (state.objects.size() >= 256)
      fail("object budget exceeded (256)");
    keys(L, {"name", "anchor_x", "anchor_y", "length", "radius", "amplitude", "color",
             "rope_color", "cycle_duration", "swings"});
    Object o;
    o.id = int(state.objects.size() + 1);
    o.name = str(L, "name", "");
    if (o.name.empty())
      o.name = "object_" + std::to_string(o.id);
    for (auto &a : state.objects)
      if (a.name == o.name)
        fail("duplicate object name: " + o.name);
    Pendulum p;
    p.anchorX = number(L, "anchor_x", 0, -20000, 20000);
    p.anchorY = number(L, "anchor_y", 0, -20000, 20000);
    p.length = number(L, "length", 100, 1, 10000);
    p.amplitude = number(L, "amplitude", 30, 0, 180);
    p.ropeColor = color(L, "rope_color", p.ropeColor);
    double cycle = number(L, "cycle_duration", 60, .001, 3600) * sampleRate;
    if (std::abs(cycle - std::round(cycle)) > 1e-7)
      fail("cycle_duration must contain whole audio sample frames");
    p.cycleSamples = uint64_t(std::llround(cycle));
    double swings = number(L, "swings", 24, 1, 10000);
    if (swings != std::floor(swings))
      fail("swings must be an integer");
    p.swings = uint64_t(swings);
    o.radius = number(L, "radius", 6, 1, 1000);
    o.color = color(L, "color", {});
    o.pendulum = p;
    evaluatePendulum(o, 0);
    o.previousX = o.x;
    o.previousY = o.y;
    bodies.push_back(b2_nullBodyId);
    state.objects.push_back(o);
    lua_pushinteger(L, o.id);
    return 1;
  }
  int random() {
    require();
    if (lua_gettop(L) != 0 && lua_gettop(L) != 2)
      fail("scene.random accepts zero or two numeric bounds");
    double lo = 0, hi = 1;
    if (lua_gettop(L) == 2) {
      if (lua_type(L, 1) != LUA_TNUMBER || lua_type(L, 2) != LUA_TNUMBER)
        fail("random bounds must be numbers");
      lo = lua_tonumber(L, 1);
      hi = lua_tonumber(L, 2);
      if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || std::abs(lo) > 1e9 ||
          std::abs(hi) > 1e9)
        fail("invalid random bounds");
    }
    uint64_t z = (rng += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= z >> 31;
    lua_pushnumber(L, lo + (hi - lo) * double(z >> 11) * 0x1.0p-53);
    return 1;
  }
  int listener(std::vector<int> &v) {
    require();
    if (!loading)
      fail("register callbacks during initialization");
    if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
      fail("expected one callback function");
    if (v.size() >= 16)
      fail("listener budget exceeded (16)");
    lua_pushvalue(L, 1);
    v.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
  }
  int listen() {
    return listener(collisionListeners);
  }
  int update() {
    return listener(updates);
  }
  int objectId(int index) {
    int id = 0;
    if (lua_type(L, index) == LUA_TSTRING) {
      size_t n = 0;
      auto *s = lua_tolstring(L, index, &n);
      if (n > 128)
        fail("object name too long");
      for (auto &o : state.objects)
        if (o.name == std::string(s, n))
          id = o.id;
    } else if (lua_isinteger(L, index)) {
      auto n = lua_tointeger(L, index);
      if (n >= 1 && n <= 256)
        id = int(n);
    }
    if (id < 1 || id > int(state.objects.size()))
      fail("unknown object handle/name");
    return id;
  }
  int get() {
    require();
    int id = objectId(1);
    auto &o = state.objects[id - 1];
    lua_newtable(L);
    field(L, "id", id);
    lua_pushlstring(L, o.name.data(), o.name.size());
    lua_setfield(L, -2, "name");
    field(L, "x", o.x);
    field(L, "y", o.y);
    field(L, "vx", o.vx);
    field(L, "vy", o.vy);
    field(L, "radius", o.radius);
    return 1;
  }
  void eventBudget() {
    require();
    if (++events > 1024)
      fail("event budget exceeded (1024 per load/tick)");
  }
  Sound sound(Sound::Kind k) {
    eventBudget();
    keys(L,
         {"id", "instrument", "frequency", "gain", "pan", "duration", "attack", "release", "at"});
    Sound s;
    s.kind = k;
    s.sequence = sequence++;
    auto inst = str(L, "instrument", "sine");
    if (inst != "sine" && inst != "bell")
      fail("instrument must be sine or bell");
    s.bell = inst == "bell";
    s.frequency = number(L, "frequency", 440, 20, 12000);
    s.gain = number(L, "gain", .2, 0, 1);
    s.pan = number(L, "pan", 0, -1, 1);
    s.duration = number(L, "duration", 1, .002, 30);
    s.attack = number(L, "attack", .005, .001, 5);
    s.release = number(L, "release", .1, .001, 10);
    double at = number(L, "at", double(now) / sampleRate, 0, 7200);
    s.at = uint64_t(std::llround(at * sampleRate));
    if (s.at < now || s.at > now + sampleRate * 30)
      fail("audio.at must be current time through 30 seconds ahead");
    return s;
  }
  void schedule(Sound s) {
    digest.add(s.kind);
    digest.add(s.at);
    digest.add(s.sequence);
    digest.add(s.id);
    digest.add(s.bell);
    digest.add(s.frequency);
    digest.add(s.gain);
    digest.add(s.pan);
    digest.add(s.duration);
    digest.add(s.attack);
    digest.add(s.release);
    digest.add(s.mask);
    mixer.schedule(s);
  }
  int play() {
    auto s = sound(Sound::Note);
    schedule(s);
    return 0;
  }
  int start() {
    auto s = sound(Sound::Start);
    if (!loading)
      fail("start persistent drones during initialization");
    if (drones.size() >= 16)
      fail("drone budget exceeded (16)");
    if (s.bell)
      fail("persistent drones use the sine instrument");
    s.id = nextDrone++;
    drones.insert(s.id);
    schedule(s);
    lua_pushinteger(L, s.id);
    return 1;
  }
  int droneId() {
    double n = number(L, "id", 0, 1, 16);
    if (n != std::floor(n) || !drones.count(int(n)))
      fail("unknown drone id");
    return int(n);
  }
  int control() {
    auto s = sound(Sound::Update);
    s.id = droneId();
    s.mask = 0;
    for (auto pair : {std::pair{"frequency", 1}, {"gain", 2}, {"pan", 4}}) {
      lua_getfield(L, 1, pair.first);
      if (!lua_isnil(L, -1))
        s.mask |= pair.second;
      lua_pop(L, 1);
    }
    schedule(s);
    return 0;
  }
  int stop() {
    auto s = sound(Sound::Stop);
    s.id = droneId();
    schedule(s);
    return 0;
  }
  int flash() {
    eventBudget();
    keys(L, {"x", "y", "radius", "duration", "color", "at"});
    Flash f;
    f.x = number(L, "x", 0, -20000, 20000);
    f.y = number(L, "y", 0, -20000, 20000);
    f.radius = number(L, "radius", 40, 1, 1000);
    f.duration = number(L, "duration", .2, .001, 2);
    f.time = number(L, "at", double(now) / sampleRate, double(now) / sampleRate,
                    double(now) / sampleRate + 2);
    f.color = color(L, "color", {255, 245, 200, 255});
    if (state.flashes.size() >= 4096)
      fail("flash history budget exceeded");
    state.flashes.push_back(f);
    digest.add(f.x);
    digest.add(f.y);
    digest.add(f.time);
    digest.add(f.duration);
    digest.add(f.radius);
    digest.add(f.color);
    return 0;
  }
  struct Hit {
    int a, b;
    double x, y, nx, ny, speed, impulse;
  };
  void step(float *out) {
    events = 0;
    totalInstructions = 1000000;
    callbackCount = 0;
    hostWork = 0;
    now = state.tick * tickSamples;
    for (auto ref : updates) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
      lua_pushnumber(L, double(now) / sampleRate);
      lua_pushnumber(L, 1.0 / 120);
      call(2, "on_update");
    }
    for (auto &o : state.objects) {
      o.previousX = o.x;
      o.previousY = o.y;
    }
    b2World_Step(world, 1.0f / 120, 4);
    ++state.tick;
    now = state.tick * tickSamples;
    for (size_t i = 0; i < bodies.size(); i++) {
      auto &o = state.objects[i];
      if (o.pendulum) {
        evaluatePendulum(o, now);
      } else {
        auto p = b2Body_GetPosition(bodies[i]);
        auto v = b2Body_GetLinearVelocity(bodies[i]);
        o.x = double(p.x) * 100;
        o.y = double(p.y) * 100;
        o.vx = double(v.x) * 100;
        o.vy = double(v.y) * 100;
      }
      digest.add(o.x);
      digest.add(o.y);
      digest.add(o.vx);
      digest.add(o.vy);
    }
    auto es = b2World_GetContactEvents(world);
    if (es.hitCount > 4096)
      fail("collision event budget exceeded");
    std::vector<Hit> hits;
    hits.reserve(es.hitCount);
    for (int i = 0; i < es.hitCount; i++) {
      auto &e = es.hitEvents[i];
      int a = int(reinterpret_cast<uintptr_t>(b2Shape_GetUserData(e.shapeIdA))),
          b = int(reinterpret_cast<uintptr_t>(b2Shape_GetUserData(e.shapeIdB)));
      double impulse = 0;
      int cap = b2Shape_GetContactCapacity(e.shapeIdA);
      if (cap > 256)
        fail("contact budget exceeded");
      std::array<b2ContactData, 256> contacts;
      int n = b2Shape_GetContactData(e.shapeIdA, contacts.data(), cap);
      for (int j = 0; j < n; j++) {
        auto &c = contacts[j];
        if ((B2_ID_EQUALS(c.shapeIdA, e.shapeIdA) && B2_ID_EQUALS(c.shapeIdB, e.shapeIdB)) ||
            (B2_ID_EQUALS(c.shapeIdB, e.shapeIdA) && B2_ID_EQUALS(c.shapeIdA, e.shapeIdB))) {
          for (int k = 0; k < c.manifold.pointCount; k++)
            impulse += c.manifold.points[k].totalNormalImpulse;
        }
      }
      Hit h{a,          b,          double(e.point.x) * 100,       double(e.point.y) * 100,
            e.normal.x, e.normal.y, double(e.approachSpeed) * 100, impulse};
      if (a > b) {
        std::swap(h.a, h.b);
        h.nx = -h.nx;
        h.ny = -h.ny;
      }
      hits.push_back(h);
    }
    std::sort(hits.begin(), hits.end(), [](auto &a, auto &b) {
      return std::tie(a.a, a.b, a.x, a.y) < std::tie(b.a, b.b, b.x, b.y);
    });
    for (auto &h : hits) {
      if (h.impulse < state.config.minImpulse)
        continue;
      auto key = std::pair{h.a, h.b};
      auto last = cooldown.find(key);
      if (last != cooldown.end() &&
          now - last->second < uint64_t(state.config.cooldown * sampleRate))
        continue;
      cooldown[key] = now;
      digest.add(now);
      digest.add(h.a);
      digest.add(h.b);
      digest.add(h.x);
      digest.add(h.y);
      digest.add(h.nx);
      digest.add(h.ny);
      digest.add(h.speed);
      digest.add(h.impulse);
      for (auto ref : collisionListeners) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_newtable(L);
        field(L, "tick", state.tick);
        field(L, "time", double(now) / sampleRate);
        field(L, "a", h.a);
        field(L, "b", h.b);
        field(L, "x", h.x);
        field(L, "y", h.y);
        field(L, "normal_x", h.nx);
        field(L, "normal_y", h.ny);
        field(L, "speed", h.speed);
        field(L, "impulse", h.impulse);
        call(1, "on_collision");
      }
    }
    double time = double(now) / sampleRate;
    while (!state.trails.empty() && state.trails.front().time < time - 2.1)
      state.trails.pop_front();
    state.flashes.erase(std::remove_if(state.flashes.begin(), state.flashes.end(),
                                       [&](auto &f) { return f.time + f.duration < time - .1; }),
                        state.flashes.end());
    if (state.tick % 4 == 0)
      for (auto &o : state.objects)
        if (o.trail > 0)
          state.trails.push_back({o.x, o.y, time, o.radius, o.color, o.id, o.trail});
    mixer.render(out, tickSamples);
  }
};
Session::Session(const std::string &s, const std::string &f, std::optional<uint64_t> seed)
    : p_(std::make_unique<Impl>()) {
  p_->init(s, f, seed);
}
Session::~Session() = default;
const Config &Session::config() const {
  return p_->state.config;
}
const Snapshot &Session::snapshot() const {
  return p_->state;
}
const Digest &Session::digest() const {
  return p_->digest;
}
uint64_t Session::tick() const {
  return p_->state.tick;
}
void Session::block(float *out) {
  p_->step(out);
}
void Session::advanceTo(uint64_t sample) {
  std::array<float, tickSamples * 2> b;
  while (tick() * tickSamples < sample)
    block(b.data());
}
} // namespace ambiance
