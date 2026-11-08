#include "ambiance/render.hpp"
#include <iostream>
int main() {
  ambiance::Session s(
      "scene.configure{}; "
      "scene.add_circle{x=120,y=40};scene.add_box{x=120,y=200,width=220,height=10};scene.on_"
      "collision(function(e) assert(e.impulse>0); audio.play{instrument='bell',frequency=330} end)",
      "capability");
  std::array<float, 800> b;
  for (int i = 0; i < 600; i++)
    s.block(b.data());
  ambiance::Canvas c(320, 180);
  c.draw(s.snapshot(), s.tick() * 400);
  std::cout << "offscreen=" << c.hash().hex() << " events=" << s.digest().hex() << "\n";
}
