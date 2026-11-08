"""Real FFmpeg integration and failure-path checks, no audio hardware or display."""
import array
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('FFmpeg and ffprobe required for release integration checks')

def run(*args, ok=True, env=None):
    p = subprocess.run([str(exe), *map(str,args)], text=True, capture_output=True, env=env, timeout=60)
    if (p.returncode == 0) != ok:
        raise AssertionError(f'{args}: code={p.returncode}\n{p.stdout}\n{p.stderr}')
    return p

def manifest(p):
    return json.loads(Path(str(p)+'.json').read_text())

def decode(p, *args):
    return subprocess.check_output([ffmpeg,'-v','error','-i',str(p),*args,'pipe:1'])

with tempfile.TemporaryDirectory(prefix='ambiance tests ',dir=os.environ.get('AMBIANCE_TEST_TMPDIR')) as directory:
    out = Path(directory)
    scene = root/'examples/bouncing_bells.lua'
    timing = root/'tests/fixtures/timing.lua'
    # Exact release fixture and repeat-export hashes, including PCM and raw uncompressed pixels.
    one = out/'timing 1 ; $(literal).mp4'
    run('export',timing,'--output',one,'--duration','10','--keep-wav')
    two = out/'timing 2.mp4'
    run('export',timing,'--output',two,'--duration','10','--keep-wav')
    m1,m2=manifest(one),manifest(two)
    for k in ('event_digest','pcm24_digest','rgba_digest','sample_frames','video_frames'):
        assert m1[k]==m2[k], k
    wav=one.with_suffix('.wav').read_bytes()
    assert wav[:4]==b'RIFF' and wav[8:12]==b'WAVE'
    assert struct.unpack_from('<I',wav,40)[0]==480000*6
    assert len(wav)==44+480000*6
    assert one.with_suffix('.wav').read_bytes()==two.with_suffix('.wav').read_bytes()
    probe=json.loads(subprocess.check_output([ffprobe,'-v','error','-count_frames','-show_streams','-of','json',str(one)]))
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    audio=next(s for s in probe['streams'] if s['codec_type']=='audio')
    assert int(video['nb_read_frames'])==300 and video['width']==1280 and video['height']==720
    assert audio['sample_rate']=='48000' and audio['channels']==2
    assert abs(float(video['duration'])-10)<1e-6 and abs(float(audio['duration'])-10)<1e-6
    # Known flash first appears on frame 15; known source pulse starts one sample after .5s.
    gray=decode(one,'-vf','scale=1:1','-pix_fmt','gray','-f','rawvideo')
    first_flash=next(i for i,v in enumerate(gray) if v>20)
    assert first_flash==15, first_flash
    data=wav[44:]
    source_onset=next(i for i in range(480000) if any(data[i*6:i*6+6]))
    assert source_onset==24001, source_onset
    decoded=array.array('f',decode(one,'-vn','-ar','48000','-ac','2','-f','f32le'))
    onset=next(i for i in range(len(decoded)//2) if max(abs(decoded[i*2]),abs(decoded[i*2+1]))>.002)
    assert abs(onset/48000-first_flash/30)<1/30, (onset,first_flash)
    print(f'Timing: video onset {first_flash/30:.6f}s; decoded AAC onset {onset/48000:.6f}s; decoded AAC samples {len(decoded)//2}; source samples 480000',flush=True)
    packets=json.loads(subprocess.check_output([ffprobe,'-v','error','-select_streams','a:0','-read_intervals','%+#1','-show_packets','-of','json',str(one)]))
    print('AAC first packet:',packets['packets'][0],flush=True)
    # Pendulum note scheduling, PCM and raw pixels repeat across independent exports.
    pendulum = root/'examples/pendulum_wave.lua'
    repeats=[]
    for i in (1,2):
        target=out/f'pendulum{i}.mp4'
        run('export',pendulum,'--output',target,'--duration','2','--width','320','--height','180')
        repeats.append(manifest(target))
    for key in ('event_digest','pcm24_digest','rgba_digest'):
        assert repeats[0][key]==repeats[1][key], key
    # WAV-only export deliberately points to unavailable encoder/probe.
    no_encoder=dict(os.environ,AMBIANCE_FFMPEG='/does/not/exist',AMBIANCE_FFPROBE='/does/not/exist')
    run('export',scene,'--output',out/'audio.wav','--duration','1',env=no_encoder)
    run('export',scene,'--output',out/'missing.mp4','--duration','1',ok=False,env=no_encoder)
    # FPS and render geometry cannot change the event or PCM pass.
    digests=[]
    for fps in (24,30,60):
        target=out/f'bells{fps}.mp4'
        run('export',scene,'--output',target,'--duration','1','--fps',fps,'--width','320','--height','240')
        digests.append((manifest(target)['event_digest'],manifest(target)['pcm24_digest']))
    assert len(set(digests))==1
    for duration in ('0.01','0','-1','nan','3601','1e2','99999999999999999999'):
        run('export',scene,'--output',out/'bad.mp4','--duration',duration,ok=False)
    run('export',scene,'--output',out/'missing'/'bad.wav','--duration','1',ok=False)
    run('export',scene,'--output',out/'bad.mp4','--width','1279','--duration','1',ok=False)
    old=one.read_bytes()
    run('export',timing,'--output',one,'--duration','1',ok=False)
    assert one.read_bytes()==old
    run('export',scene,'--output',out/'audio.wav','--duration','1','--overwrite')
    # Encoder exits after capabilities: exercise EPIPE cleanup without replacing a destination.
    broken=out/'broken encoder'
    broken.write_text('#!/bin/sh\ncase "$1" in\n-hide_banner) echo " libx264 aac ";;\n-version) echo fixture;;\n*) exit 7;;\nesac\n')
    broken.chmod(0o700)
    p=run('export',scene,'--output',out/'broken.mp4','--duration','2','--width','320','--height','180',ok=False,env=dict(os.environ,AMBIANCE_FFMPEG=str(broken)))
    assert 'pipe' in p.stderr.lower() or 'failed' in p.stderr.lower(),p.stderr
    assert not (out/'broken.mp4').exists()
    silent=out/'silent.lua';silent.write_text('scene.configure{duration=1}')
    run('export',silent,'--output',out/'silence.wav','--duration','1')
    assert set((out/'silence.wav').read_bytes()[44:])=={0}
    drone=root/'examples/orbital_drone.lua'
    run('export',drone,'--output',out/'drone1.wav','--duration','2')
    run('export',drone,'--output',out/'drone2.wav','--duration','2')
    assert (out/'drone1.wav').read_bytes()==(out/'drone2.wav').read_bytes()
    assert any((out/'drone1.wav').read_bytes()[44:])
    # Interrupt a real long encode; old artifacts stay intact, job files disappear.
    target=out/'cancelled.mp4';target.write_bytes(b'original')
    child=subprocess.Popen([str(exe),'export',str(scene),'--output',str(target),'--duration','600','--overwrite'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    time.sleep(.35);child.send_signal(signal.SIGINT)
    assert child.wait(timeout=10)==130, 'cancellation failed (check free disk space for 600-second source WAV)'
    assert target.read_bytes()==b'original'
    assert not list(out.glob('.ambiance-*'))
    # Script error paths remain bounded, with actionable filename/callback context.
    for body in ('while true do end', 'scene.configure{};scene.on_update(function()while true do end end)',
                 'scene.configure{unknown=3}', 'scene.configure{};scene.on_update(function()audio.play{at=0}end)'):
        bad=out/'bad.lua';bad.write_text(body)
        p=run('validate',bad,'--simulate','1',ok=False)
        assert 'Error:' in p.stderr
    print('Export contracts passed: exact 720p counts, deterministic two-pass output, timing, WAV without encoders, paths, overwrite, missing/broken encoders, cancellation, scripts',flush=True)
