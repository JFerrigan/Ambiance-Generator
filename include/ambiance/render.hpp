#pragma once
#include "core.hpp"
#include <SDL3/SDL.h>
namespace ambiance {
class Canvas {
  SDL_Surface *surface_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;

public:
  Canvas(int width, int height);
  ~Canvas();
  Canvas(const Canvas &) = delete;
  Canvas &operator=(const Canvas &) = delete;
  void draw(const Snapshot &, uint64_t sample);
  const void *pixels() const {
    return surface_->pixels;
  }
  int pitch() const {
    return surface_->pitch;
  }
  int width() const {
    return surface_->w;
  }
  int height() const {
    return surface_->h;
  }
  Digest hash() const;
};
} // namespace ambiance
