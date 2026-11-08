// Run explicitly on a Mac desktop; never opened by headless CTest.
#include "ambiance/app.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace ambiance;
int main() {
  auto directory =
      std::filesystem::temp_directory_path() / ("ambiance-ui-" + std::to_string(getpid()));
  std::filesystem::create_directory(directory);
  auto path = directory / "scene.lua";
  auto write = [&](const std::string &text) {
    std::ofstream f(path);
    f << text;
  };
  const std::string good =
      "scene.configure{duration=.1};scene.add_circle{x=200,y=100};scene.on_update(function(t) if "
      "t>1 then error('working scene survived rejected reload') end end)";
  write(good);
  Options o;
  o.command = "preview";
  o.script = path.string();
  o.noAudio = true;
  o.endless = true;
  o.previewSeconds = 4;
  std::ostringstream log;
  auto *previous = std::cerr.rdbuf(log.rdbuf());
  std::thread driver([&] {
    auto start = std::chrono::steady_clock::now();
    auto at = [&](int ms) { std::this_thread::sleep_until(start + std::chrono::milliseconds(ms)); };
    auto key = [](SDL_Keycode value) {
      SDL_Event event{};
      event.type = SDL_EVENT_KEY_DOWN;
      event.key.key = value;
      SDL_PushEvent(&event);
    };
    at(200);
    key(SDLK_SPACE);
    at(400);
    key(SDLK_SPACE);
    at(550);
    write("scene.configure{unknown_key=1}");
    at(600);
    key(SDLK_R);
    at(900);
    key(SDLK_L);
    at(2100);
    write("scene.configure{duration=.1};scene.add_circle{x=250,y=100}");
    key(SDLK_L);
    at(2700);
    key(SDLK_ESCAPE);
  });
  int result = 0;
  try {
    previewScene(o);
  } catch (const std::exception &e) {
    log << e.what();
    result = 1;
  }
  driver.join();
  std::cerr.rdbuf(previous);
  std::cout << log.str();
  for (auto expected :
       {"Paused at sample", "Resumed at sample", "Restarted", "unknown setting: unknown_key",
        "working scene survived rejected reload", "Reloaded successfully"})
    if (log.str().find(expected) == std::string::npos) {
      std::cerr << "Missing native preview behavior: " << expected << "\n";
      result = 1;
    }
  std::filesystem::remove_all(directory);
  if (!result)
    std::cout << "Native preview Space/R/L/Esc and failed reload preservation passed\n";
  return result;
}
