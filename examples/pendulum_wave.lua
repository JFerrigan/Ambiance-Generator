-- Ideal periodic motion: prescribed periods, with small-angle pendulum length ratios.
-- All twelve weights are released from rest together; the geometry repeats every 60 s.
scene.configure{
  width=1280, height=720, duration=60,
  background={10,14,23}
}
local sample_rate, cycle_samples = 48000, 60*48000
local colors = {
  {110,178,220}, {105,196,208}, {110,204,184}, {145,205,158},
  {183,207,140}, {216,202,131}, {229,180,129}, {229,151,143},
  {214,140,175}, {185,145,209}, {151,154,222}, {127,174,227}
}
-- D major pentatonic: D E F# A B, ascending from D3 to E5.
local midi = {50,52,54,57,59,62,64,66,69,71,74,76}
local pendulums = {}
for i=1,12 do
  local swings = 23+i
  scene.add_pendulum{
    name='pendulum_'..i, anchor_x=640, anchor_y=80,
    length=520*(24/swings)^2, radius=6, amplitude=30,
    color=colors[i], rope_color={48,58,74},
    cycle_duration=60, swings=swings
  }
  pendulums[i] = {swings=swings, frequency=440*2^((midi[i]-69)/12), next_crossing=0}
end

scene.on_update(function(time, dt)
  local first = math.floor(time*sample_rate+0.5)
  local last = first+400 -- upcoming fixed tick is [first,last)
  for _,p in ipairs(pendulums) do
    while true do
      local cycle = p.next_crossing//(2*p.swings)
      local crossing = p.next_crossing%(2*p.swings)
      local denominator = 4*p.swings
      -- ceil of the exact rational crossing; no accumulated floating-point time.
      local at = cycle*cycle_samples +
        ((2*crossing+1)*cycle_samples+denominator-1)//denominator
      if at>=last then break end
      if at>=first then
        audio.play{
          instrument='bell', frequency=p.frequency, gain=0.09, pan=0,
          duration=0.65, attack=0.003, release=0.5, at=at/sample_rate
        }
      end
      p.next_crossing = p.next_crossing+1
    end
  end
end)
