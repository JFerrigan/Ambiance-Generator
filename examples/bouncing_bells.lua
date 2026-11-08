-- Logical pixels, seconds, 120 physics updates/second. Change this file, then press L.
scene.configure{schema=1, width=1280, height=720, duration=600, seed=42,
  gravity_y=190, background={10,15,27}, min_impulse=0.012, cooldown=0.14}
local wall={41,56,78}
scene.add_box{name='floor',x=640,y=681,width=1180,height=16,color=wall}
scene.add_box{name='ceiling',x=640,y=39,width=1180,height=16,color=wall}
scene.add_box{name='left',x=49,y=360,width=16,height=650,color=wall}
scene.add_box{name='right',x=1231,y=360,width=16,height=650,color=wall}
local notes={220,246.9417,293.6648,329.6276,369.9944,440,493.8833,587.3295}
local pitches={}
local palette={{116,199,218},{218,173,113},{162,157,218},{130,202,171}}
for i=1,20 do
  local id=scene.add_circle{name='bell_'..i,x=115+(i-1)%10*115,
    y=130+math.floor((i-1)/10)*180+scene.random(-24,24),
    vx=scene.random(-135,135),vy=scene.random(-80,80),radius=scene.random(9,17),
    restitution=0.99,color=palette[(i-1)%4+1],trail=0.65}
  pitches[id]=notes[(i-1)%#notes+1]
end
scene.on_collision(function(e)
  local frequency=pitches[e.a] or pitches[e.b]
  if not frequency then return end
  audio.play{instrument='bell',frequency=frequency,duration=2.2,
    gain=math.min(0.34,0.06+e.impulse*0.18),pan=e.x/640-1,release=0.8}
  visual.flash{x=e.x,y=e.y,radius=25,duration=0.18,color={235,211,159}}
end)
