# Arranges and masters the reel soundtrack from rendered Groove blocks, and
# writes the beat/envelope data the motion page syncs to.
import numpy as np, json, scipy.signal as ss, wave, os, sys
HERE=os.path.dirname(os.path.abspath(__file__)); BLOCKS=os.environ.get('BLOCKS_DIR', os.path.join(HERE,'..','build','blocks'))
OUT=os.path.join(HERE,'..','build'); MOTION=os.path.join(HERE,'..','motion')
SR=44100; BAR=75600; BEAT=BAR//4
TOTAL=int(17.5*BAR)   # exactly 30.000 s
def blk(i): return np.fromfile(os.path.join(BLOCKS,f'{i}.f32'),dtype=np.float32).astype(np.float64)
db=lambda g:10**(g/20)
# track lanes: 0 drums 1 bass 2 keys 3 guitar 4 pad/synth 5 vox 6 fx 7 cymbals
clips=[ # block, track, start_bar, bars, gain_db, name
 (522,2,0,4,-3,'Skyline Signal'),(201,6,0,2,-4,'White Swell'),
 (501,0,2,2,0,'Club Floor'),(508,1,2,2,0,'Skyline Verse Bass'),(105,5,3,1,-1,'Feel It'),
 (502,0,4,4,0,'Club Motion'),(508,1,4,4,0,'Skyline Verse Bass'),(511,2,4,4,-6,'Skyline Verse Piano'),
 (523,3,4,4,-2,'Skyline Verse Guitar'),(526,4,4,4,-4,'Skyline Verse Pad'),(435,5,6,2,-1,'Turn Up the Glow'),
 (505,0,8,2,0,'Club Lift Two'),(520,1,8,2,0,'Skyline Build Bass'),(521,2,8,2,-5,'Skyline Build Piano'),
 (22,6,8,2,-3,'Riser'),(495,5,9,1,-2,"Let's Go"),
 (500,0,10,2,0,'Club Foundation'),(503,0,12,4,0,'Club Turnaround'),(509,1,10,6,0,'Skyline Chorus Bass'),
 (514,2,10,4,-5,'Skyline Hook A'),(515,2,14,2,-5,'Skyline Hook B'),(524,3,10,6,-2,'Skyline Chorus Guitar'),
 (527,4,10,4,-4,'Skyline Chorus Pad'),(529,4,14,2,-5,'Skyline Final Chords'),(86,7,10,1,-1,'Crash Down'),
 (112,6,10,1,-4,'Impact'),(108,5,12,2,-1,'Light the Night'),(496,5,15,1,-2,'Hey Yo'),
 (531,2,16,1.5,-3,'Skyline Closing Piano'),(303,7,16,1.5,-2,'Big Crash'),(200,6,16,1.5,-3,'Sub Drop'),(112,6,16,1,-4,'Impact'),
 (500,0,16,0.25,0,'Club Foundation'),(509,1,16,0.5,0,'Skyline Chorus Bass'),
]
stems=np.zeros((8,TOTAL))
for b,t,s,n,g,name in clips:
  src=blk(b); st=int(s*BAR); ln=int(n*BAR); out=np.zeros(ln)
  for a in range(0,ln,len(src)): out[a:a+len(src)]=src[:min(len(src),ln-a)]
  # short fade at clip edge to avoid clicks
  f=min(256,ln//4); out[-f:]*=np.linspace(1,0,f)
  e=min(st+ln,TOTAL); stems[t,st:e]+=out[:e-st]*0.5*db(g)
# intro filter sweep on keys bars 0-2 (lowpass opening 400Hz->16k)
def sweep(x,a,b,f0,f1):
  seg=x[a:b].copy(); n=len(seg); out=np.zeros(n); blk_=1024; zi=None
  for i in range(0,n,blk_):
    fc=f0*(f1/f0)**(i/n); bb,aa=ss.butter(2,min(fc,19000)/(SR/2))
    if zi is None: zi=ss.lfilter_zi(bb,aa)*0
    out[i:i+blk_],zi=ss.lfilter(bb,aa,seg[i:i+blk_],zi=zi)
  x[a:b]=out
sweep(stems[2],0,2*BAR,350,16000); stems[2][:2*BAR]*=1.9
# sidechain pump keyed on kick (drum low band) for pad/guitar/keys during drop
lowk=ss.sosfilt(ss.butter(4,120/(SR/2),output='sos'),stems[0])
env=np.abs(lowk); env=ss.sosfilt(ss.butter(1,8/(SR/2),output='sos'),env); env/=env.max()+1e-9
duck=1-0.45*np.clip(env*2.2,0,1)
for t in (3,4): stems[t,10*BAR:16*BAR]*=duck[10*BAR:16*BAR]
# ending: fade last 1.5 bars to silence
fade=np.ones(TOTAL); fl=TOTAL-16*BAR; fade[16*BAR:]=np.linspace(1,0,fl)**1.6
stems*=fade
# stereo: width per track via Haas + pan
pan=[0,0,-0.12,0.35,-0.3,0,0.15,0.2]; haas=[0,0,8,14,18,0,22,10]
L=np.zeros(TOTAL); R=np.zeros(TOTAL)
for t in range(8):
  x=stems[t]; d=int(haas[t]*SR/1000)
  xd=np.concatenate([np.zeros(d),x[:TOTAL-d]]) if d else x
  gl=np.sqrt(0.5*(1-pan[t])); gr=np.sqrt(0.5*(1+pan[t]))
  L+=x*gl*1.41*(0.85 if d else 1)+ (xd*0.15 if d else 0); R+=xd*gr*1.41*(0.85 if d else 1)+(x*0.15 if d else 0)
# simple reverb-ish ambience on vox + keys (feedback delay at 3/16)
def echo(x,dly,fb,mix):
  y=x.copy(); d=int(dly)
  for k in range(1,5): y[d*k:]+=x[:-d*k]*(fb**k)*mix
  return y
vx=echo(stems[5],BEAT*0.75,0.45,0.5)-stems[5]
L+=np.concatenate([vx[:]])*0.5; R+=np.concatenate([np.zeros(int(BEAT*0.375)),vx[:-int(BEAT*0.375)]])*0.5
mix=np.stack([L,R])
# glue: gentle bus compression (rms-based)
rms=np.sqrt(ss.sosfilt(ss.butter(1,6/(SR/2),output='sos'),(mix**2).mean(0))+1e-12)
thr=0.25; gain=np.where(rms>thr,(thr/rms)**0.35,1.0); mix*=gain
# lookahead limiter to -1 dBFS
peak=np.abs(mix).max(0); ceil=10**(-1/20)
look=220; pk=ss.maximum_filter1d(peak,look*2) if hasattr(ss,'maximum_filter1d') else None
from scipy.ndimage import maximum_filter1d, uniform_filter1d
pk=maximum_filter1d(peak,size=look*2+1)
target=np.minimum(1, ceil/(pk+1e-9))
# normalise loudness first: aim rms
mix*= (0.20/ np.sqrt((mix[:, :int(16*BAR)]**2).mean()))
peak=np.abs(mix).max(0); pk=maximum_filter1d(peak,size=look*2+1); g=np.minimum(1,ceil/(pk+1e-9))
g=uniform_filter1d(g,size=look); mix*=g
mix=np.clip(mix,-ceil,ceil)
mix[:,-64:]*=np.linspace(1,0,64)
pcm=(mix.T*32767).astype(np.int16)
with wave.open(os.path.join(OUT,'music.wav'),'wb') as w:
  w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
# ---------- analysis for visuals (60 fps) ----------
FPS=60; N=30*FPS; hop=SR//FPS
def envf(x,smooth=0.35):
  e=np.array([np.sqrt((x[i*hop:(i+1)*hop]**2).mean()) for i in range(N)])
  e/= (np.percentile(e,99.5)+1e-9); e=np.clip(e,0,1.5)
  out=np.zeros(N); v=0
  for i in range(N): v=max(e[i], v*smooth+e[i]*(1-smooth)) if e[i]>v else v*0.82+e[i]*0.18; out[i]=v
  return out.round(3).tolist()
def onsets(x,lo=None,hi=None,thr=0.25,mingap=0.09):
  y=x
  if lo or hi:
    sos=ss.butter(4,[lo/(SR/2),hi/(SR/2)],'band',output='sos') if lo and hi else ss.butter(4,(hi or lo)/(SR/2),'low' if hi else 'high',output='sos')
    y=ss.sosfilt(sos,x)
  h=256; e=np.array([np.sqrt((y[i:i+h]**2).mean()) for i in range(0,len(y)-h,h)])
  fl=np.maximum(0,np.diff(e,prepend=0)); fl/= fl.max()+1e-9
  pk,_=ss.find_peaks(fl,height=thr,distance=int(mingap*SR/h))
  return [round(float(p*h/SR),3) for p in pk]
data=dict(fps=FPS,bpm=140,bar=BAR/SR,beat=BEAT/SR,
  env={k:envf(stems[i]) for i,k in enumerate(['drums','bass','keys','guitar','pad','vox','fx','cym'])},
  master=envf(mix.mean(0)),
  kick=onsets(stems[0],hi=140,thr=0.3,mingap=0.2), snare=onsets(stems[0],lo=1500,hi=6000,thr=0.35,mingap=0.1),
  keys=onsets(stems[2],thr=0.2,mingap=0.1), vox=onsets(stems[5],thr=0.25,mingap=0.15),
  clips=[dict(block=b,track=t,start=s,bars=n,name=nm) for b,t,s,n,g,nm in clips])
json.dump(data,open(os.path.join(OUT,'sync.json'),'w'))
print('kicks',len(data['kick']),data['kick'][:12]); print('snare',len(data['snare'])); print('keys',len(data['keys']),data['keys'][:10]); print('vox',data['vox'])
print('peak',np.abs(mix).max(),'rms',np.sqrt((mix**2).mean()))

# ---------- block names, categories and waveform peaks for the cards ----------
cats=['Drums','Kicks','Snares','Hats','Claps','Cymbals','Perc','Fills','Bass','Keys','Organ','Guitar','Synth','Pads','Stabs','Vocals','Scratch','FX']
rows={int(l.split('\t')[0]):l.rstrip('\n').split('\t') for l in open(os.path.join(OUT,'blocks.tsv'))}
bycat={c:[i for i in sorted(rows) if rows[i][2]==c] for c in cats}
lib=[i for c in cats for i in bycat[c][:3]]
info={}
for i in set(lib)|{c['block'] for c in data['clips']}:
  x=blk(i); bars=int(rows[i][3]); p=np.array([q.max() for q in np.array_split(np.abs(x),bars*48)]); p=p/(p.max()+1e-9)
  info[i]=dict(name=rows[i][1],cat=cats.index(rows[i][2]),bars=bars,peaks=[round(float(v),2) for v in p])
data['blocks']=info; data['lib']=lib; data['nblocks']=len(rows)
open(os.path.join(MOTION,'sync.js'),'w').write('window.SYNC='+json.dumps(data)+';')
