#include "ambiance/app.hpp"
#include "ambiance/device.hpp"
#include "ambiance/render.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
namespace ambiance {
void previewScene(const Options &o) {
  SDL_SetAppMetadata("Ambiance Generator", "1.0.0", "local.ambiance.generator");
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());
  struct UI {
    SDL_Window *w = nullptr;
    SDL_Renderer *r = nullptr;
    SDL_Texture *t = nullptr;
    ~UI() {
      SDL_DestroyTexture(t);
      SDL_DestroyRenderer(r);
      SDL_DestroyWindow(w);
      SDL_Quit();
    }
  } ui;
  ui.w = SDL_CreateWindow("Ambiance Generator", 1280, 820, SDL_WINDOW_RESIZABLE);
  if (!ui.w)
    throw std::runtime_error(SDL_GetError());
  ui.r = SDL_CreateRenderer(ui.w, nullptr);
  if (!ui.r)
    throw std::runtime_error(SDL_GetError());
  SDL_SetRenderVSync(ui.r, 1);
  ui.t = SDL_CreateTexture(ui.r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 1280, 720);
  if (!ui.t)
    throw std::runtime_error(SDL_GetError());
  Canvas canvas(1280, 720);
  std::string source = readScript(o.script), message, lastRuntimeError;
  auto scene = std::make_unique<Session>(source, o.script, o.seed);
  auto transport = std::make_unique<Transport>(std::move(scene), o.noAudio, o.latencyMs);
  auto start = std::chrono::steady_clock::now();
  bool quit = false;
  uint64_t seenUnderruns = 0;
  std::vector<double> times;
  times.reserve(36000);
  double worstTick = 0;
  uint64_t totalUnderruns = 0;
  while (!quit && !cancelled) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT)
        quit = true;
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_ESCAPE)
          quit = true;
        if (e.key.key == SDLK_SPACE) {
          transport->toggle();
          message.clear();
          std::cerr << (transport->playing() ? "Resumed" : "Paused") << " at sample "
                    << transport->sample() << "\n";
        }
        if (e.key.key == SDLK_R || e.key.key == SDLK_L) {
          try {
            auto nextSource =
                e.key.key == SDLK_L
                    ? readScript(o.script)
                    : source; // validate candidate callbacks before disturbing playback
            auto candidate = std::make_unique<Session>(nextSource, o.script, o.seed);
            candidate->advanceTo(4800);
            auto fresh = std::make_unique<Session>(nextSource, o.script, o.seed);
            worstTick = std::max(worstTick, transport->worstTickMs());
            totalUnderruns += transport->underruns();
            transport.reset();
            transport = std::make_unique<Transport>(std::move(fresh), o.noAudio, o.latencyMs);
            source = std::move(nextSource);
            seenUnderruns = 0;
            message = e.key.key == SDLK_L ? "Reloaded successfully" : "Restarted";
            lastRuntimeError.clear();
            std::cerr << message << "\n";
          } catch (const std::exception &err) {
            message = err.what();
            std::cerr << message << "\n";
            if (!transport) {
              transport = std::make_unique<Transport>(
                  std::make_unique<Session>(source, o.script, o.seed), o.noAudio, o.latencyMs);
            }
          }
        }
      }
    }
    auto frameStart = std::chrono::steady_clock::now();
    auto err = transport->error();
    if (!err.empty()) {
      message = err;
      if (err != lastRuntimeError) {
        std::cerr << err << "\n";
        lastRuntimeError = err;
      }
    }
    if (transport->underruns() != seenUnderruns) {
      seenUnderruns = transport->underruns();
      message = "Audio underrun: rebuffered and paused. Press Space to resume.";
      std::cerr << message << "\n";
    }
    auto sample = transport->sample();
    auto snapshot = transport->snapshot(sample);
    canvas.draw(snapshot, sample);
    SDL_UpdateTexture(ui.t, nullptr, canvas.pixels(), canvas.pitch());
    int w, h;
    SDL_GetRenderOutputSize(ui.r, &w, &h);
    SDL_SetRenderDrawColor(ui.r, 16, 21, 31, 255);
    SDL_RenderClear(ui.r);
    double scale = std::min(double(w) / 1280, double(std::max(1, h - 116)) / 720);
    SDL_FRect dst{float((w - 1280 * scale) / 2), 54, float(1280 * scale), float(720 * scale)};
    SDL_RenderTexture(ui.r, ui.t, nullptr, &dst);
    SDL_SetRenderDrawColor(ui.r, 205, 220, 240, 255);
    SDL_SetRenderScale(ui.r, 1.5, 1.5);
    std::ostringstream status;
    status << "Ambiance Generator   " << int(sample / 48000) / 60 << ":";
    if ((sample / 48000) % 60 < 10)
      status << '0';
    status << (sample / 48000) % 60 << "   "
           << (transport->finished()  ? "ENDED"
               : transport->playing() ? "PLAYING"
                                      : "PAUSED")
           << "   underruns " << transport->underruns();
    SDL_RenderDebugText(ui.r, 12, 10, status.str().c_str());
    SDL_RenderDebugText(ui.r, 12, float(h - 54) / 1.5,
                        "Space pause/resume   R restart   L reload   Esc exit");
    // ASCII status is outside the scene. Errors also go to stderr with full source context.
    std::string visible = message.substr(0, message.find('\n'));
    size_t chars = std::max(10, w / 12 - 4);
    for (size_t i = 0; i < std::min<size_t>(visible.size(), chars * 2); i += chars)
      SDL_RenderDebugText(ui.r, 12, float(h - 34) / 1.5 + float(i / chars) * 10,
                          visible.substr(i, chars).c_str());
    SDL_SetRenderScale(ui.r, 1, 1);
    double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart)
            .count();
    if (times.size() < 36000)
      times.push_back(ms);
    SDL_RenderPresent(ui.r);
    SDL_Delay(1);
    if (o.previewSeconds > 0 &&
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >=
            o.previewSeconds)
      quit = true;
  }
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::sort(times.begin(), times.end());
  std::cerr << "Preview elapsed=" << elapsed << "s, scene=" << transport->sample() / 48000.0
            << "s, presentation p95="
            << (times.empty() ? 0 : times[size_t((times.size() - 1) * .95)])
            << "ms, worker max=" << std::max(worstTick, transport->worstTickMs())
            << "ms, underruns=" << totalUnderruns + transport->underruns() << "\n";
}
} // namespace ambiance
