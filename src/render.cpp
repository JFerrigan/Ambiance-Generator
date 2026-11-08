#include "ambiance/render.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ambiance {
Canvas::Canvas(int w, int h) {
  surface_ = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (!surface_)
    throw std::runtime_error(SDL_GetError());
  renderer_ = SDL_CreateSoftwareRenderer(surface_);
  if (!renderer_) {
    SDL_DestroySurface(surface_);
    throw std::runtime_error(SDL_GetError());
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}
Canvas::~Canvas() {
  SDL_DestroyRenderer(renderer_);
  SDL_DestroySurface(surface_);
}
void Canvas::draw(const Snapshot &s, uint64_t sample) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  double scale = std::min(double(width()) / s.config.width, double(height()) / s.config.height);
  double ox = (width() - s.config.width * scale) / 2, oy = (height() - s.config.height * scale) / 2;
  SDL_FRect bounds{float(ox), float(oy), float(s.config.width * scale),
                   float(s.config.height * scale)};
  auto set = [&](Color c, double alpha = 1.0) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, uint8_t(std::clamp(alpha, 0.0, 1.0) * c.a));
  };
  set(s.config.background);
  SDL_RenderFillRect(renderer_, &bounds);
  SDL_Rect clip{int(std::ceil(ox)), int(std::ceil(oy)), int(s.config.width * scale),
                int(s.config.height * scale)};
  SDL_SetRenderClipRect(renderer_, &clip);
  auto disk = [&](double x, double y, double r, Color c, double alpha) {
    x = ox + x * scale;
    y = oy + y * scale;
    r *= scale;
    set(c, alpha);
    int top = std::max(clip.y, int(std::ceil(y - r))),
        bottom = std::min(clip.y + clip.h - 1, int(std::floor(y + r)));
    for (int py = top; py <= bottom; py++) {
      double dy = py - y, dx = std::sqrt(std::max(0.0, r * r - dy * dy));
      SDL_RenderLine(renderer_, float(x - dx), float(py), float(x + dx), float(py));
    }
  };
  double time = double(sample) / sampleRate;
  for (auto &t : s.trails) {
    double age = time - t.time;
    if (age >= 0 && age < t.life)
      disk(t.x, t.y, t.radius * .65, t.color, .22 * (1 - age / t.life));
  }
  for (auto &o : s.objects) {
    double a = std::clamp((double(sample) / tickSamples) - (double(s.tick) - 1), 0.0, 1.0);
    double x = o.previousX + (o.x - o.previousX) * a, y = o.previousY + (o.y - o.previousY) * a;
    if (o.radius) {
      disk(x, y, o.radius + 3, o.color, .12);
      disk(x, y, o.radius, o.color, 1);
      disk(x - o.radius * .25, y - o.radius * .3, o.radius * .22, {255, 255, 255, 255}, .35);
    } else {
      set(o.color);
      SDL_FRect r{float(ox + (x - o.width / 2) * scale), float(oy + (y - o.height / 2) * scale),
                  float(o.width * scale), float(o.height * scale)};
      SDL_RenderFillRect(renderer_, &r);
    }
  }
  for (auto &f : s.flashes) {
    double age = time - f.time;
    if (age >= 0 && age < f.duration) {
      double a = 1 - age / f.duration;
      disk(f.x, f.y, f.radius * (1 + .5 * (1 - a)), f.color, .45 * a);
    }
  }
  SDL_SetRenderClipRect(renderer_, nullptr);
  SDL_RenderPresent(renderer_);
}
Digest Canvas::hash() const {
  Digest d;
  for (int y = 0; y < height(); y++)
    d.bytes(static_cast<const char *>(pixels()) + y * pitch(), width() * 4);
  return d;
}
} // namespace ambiance
