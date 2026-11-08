-- A persistent sine drone follows the position and speed of one bouncing orb.
scene.configure{schema=1,width=1280,height=720,duration=600,seed=73,
  gravity_y=55,background={11,18,28},min_impulse=0.005,cooldown=0.2}
local edge={42,65,78}
scene.add_box{x=640,y=680,width=1160,height=12,color=edge}
scene.add_box{x=640,y=40,width=1160,height=12,color=edge}
scene.add_box{x=60,y=360,width=12,height=640,color=edge}
scene.add_box{x=1220,y=360,width=12,height=640,color=edge}
local orb=scene.add_circle{name='orb',x=350,y=200,vx=180,vy=-65,radius=28,
  restitution=1,trail=2,color={129,211,195}}
local drone=audio.start{frequency=110,gain=0.18,pan=-0.45,attack=0.5}
local overtone=audio.start{frequency=165,gain=0.08,attack=1}
scene.on_update(function(time,dt)
  local o=scene.get_object(orb)
  local speed=math.sqrt(o.vx*o.vx+o.vy*o.vy)
  local pitch=82.4069+speed*0.16
  audio.update{id=drone,frequency=pitch,gain=0.14+math.min(speed/2000,0.12),pan=o.x/640-1}
  audio.update{id=overtone,frequency=pitch*1.5,pan=1-o.x/640}
end)
scene.on_collision(function(e)
  visual.flash{x=e.x,y=e.y,radius=55,duration=0.5,color={151,226,210}}
  audio.play{instrument='sine',frequency=329.6276,gain=0.06,duration=1.5,attack=0.04,release=1,pan=e.x/640-1}
end)
