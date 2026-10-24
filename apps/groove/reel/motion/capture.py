# Renders motion/index.html frame by frame and pipes it to ffmpeg.
# usage: capture.py FIRST LAST OUT.mp4 [SUBFRAMES]
import sys, base64, subprocess, time, pathlib, os
from playwright.sync_api import sync_playwright
t0f=int(sys.argv[1]); t1f=int(sys.argv[2]); out=sys.argv[3]; sub=int(sys.argv[4]) if len(sys.argv)>4 else 1
mode=sys.argv[5] if len(sys.argv)>5 else 'video'
with sync_playwright() as p:
  br=p.chromium.launch(executable_path=os.environ.get('CHROMIUM_EXECUTABLE'), args=['--allow-file-access-from-files','--disable-web-security'])
  pg=br.new_page(viewport={'width':1920,'height':1080})
  pg.on('console', lambda m: print('console:',m.text))
  pg.on('pageerror', lambda e: print('ERR',e))
  pg.goto(pathlib.Path(__file__).resolve().with_name('index.html').as_uri()); pg.evaluate('window.ready')
  if mode=='stills':
    for f in range(t0f,t1f):
      pass
  ff=None
  if mode=='video':
    ff=subprocess.Popen([os.environ.get('FFMPEG','ffmpeg'),'-v','error','-y','-f','image2pipe','-framerate','60','-i','-','-vf','scale=in_range=full:out_range=tv','-color_range','tv','-c:v','libx264','-preset','medium','-crf','14','-pix_fmt','yuv420p',out],stdin=subprocess.PIPE)
  st=time.time()
  frames=range(t0f,t1f) if mode=='video' else [int(x) for x in sys.argv[6].split(',')]
  for f in frames:
    d=pg.evaluate(f"(renderFrame({f}/60,{sub}), document.getElementById('out').toDataURL('image/jpeg',0.95))")
    jpg=base64.b64decode(d.split(',')[1])
    if ff: ff.stdin.write(jpg)
    else: open(f'{out}/f{f:04d}.jpg','wb').write(jpg)
    if mode=='video' and (f-t0f+1)%60==0: print('rendered',f-t0f+1,'/',t1f-t0f,flush=True)
  if ff: ff.stdin.close(); ff.wait()
  print('done',len(frames),'frames in',round(time.time()-st,1),'s')
  br.close()
