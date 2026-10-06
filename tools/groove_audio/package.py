from pathlib import Path
import csv,json,struct,zipfile
import numpy as np,soundfile as sf
from common import prepare_work, work_arguments
parser = work_arguments('Validate exports, write MIDI and recipe notes, and package local ZIPs.')
args = parser.parse_args()
r = prepare_work(args)
s=r/'synth-study';v=r/'vocal-samples'
rows=list(csv.DictReader((v/'sample-index.csv').open()))
previous=0
for row in rows:
 p=v/row['file'];x,sr=sf.read(p,always_2d=True);meta=sf.info(p)
 assert meta.subtype=='PCM_24' and sr==sf.info(r/'stems/vocals_full.wav').samplerate
 assert np.isfinite(x).all() and 0<np.max(abs(x))<=.892
 assert np.max(abs(x[[0,-1]]))<1e-6
 assert float(row['source_start_seconds'])>=previous-.001
 assert abs(len(x)/sr-float(row['duration_seconds']))<.001
 previous=float(row['source_end_seconds'])
assert len(rows)>0 and len(rows)==len(list(v.glob('*.wav')))
a=json.loads((s/'reconstruction-analysis.json').read_text());bpm=a['study_bpm']
recipe=[f'{r.name}: Groove feasibility sketch','Source excerpt: %.3f to %.3f seconds.'%(a['source_start_seconds'],a['source_start_seconds']+a['duration_seconds']),f'Tempo: {bpm:.3f} BPM. Tonal center estimated as A minor.','The source beat timing varies; this sketch uses one fixed local tempo.','Rendered with the existing apps/groove/synth.c, without patch changes.','The following are estimated note rows, not an exact transcription.','Use N(...) rows with 8 bars, 2 steps per beat; each pattern contains 64 eighth-note steps.','Bass: I_MOOG, no FX, hold=0.7.', 'Lead variants: I_SAW / I_HPIANO / I_PLUCK, X_ECHO, hold=0.8.','Synth audio is mono. CSV files contain start_seconds,duration_seconds,MIDI_note,gain.','']
for key,label in [('estimated_bass_eighth_notes','BASS'),('estimated_dominant_melody_eighth_notes','LEAD')]:
 seq=a[key];tokens=[n if label=='BASS' or i==0 or n!=seq[i-1] or n=='-' else '_' for i,n in enumerate(seq)]
 recipe += [label+':',' '.join(tokens),'']
recipe+=['Estimated pad chords, one per bar: '+', '.join(a['estimated_chords_per_bar']), 'Pad: I_PAD with X_ROOM; gate 3.7 beats per bar.','Generic rhythm, KIT_909:','k=x...x...x...x... c=....x.......x... h=x.x.x.x.x.x.x.x. o=..x...x...x...x.','The drum pattern is generic; it was not transcribed from the recording.','',a['limits'],'MIDI uses conventional GM programs as editing placeholders; Groove WAVs use the actual C synth.','Current Groove blocks are generated from recipes; importing these vocal WAVs requires sample playback support.']
(s/'recipe-notes.txt').write_text('\n'.join(recipe)+'\n')
# Editable Standard MIDI file; tempo and note events mirror the rendered sketch.
def vlq(n):
 out=[n&127];n>>=7
 while n:out.insert(0,(n&127)|128);n>>=7
 return bytes(out)
def chunk(events):
 data=bytearray();previous=0
 for tick,priority,event in sorted(events,key=lambda x:(x[0],x[1])):
  data+=vlq(tick-previous)+event;previous=tick
 data+=b'\x00\xff\x2f\x00';return b'MTrk'+struct.pack('>I',len(data))+data
tracks=[];ppq=480
us=round(60e6/bpm)
tracks.append(chunk([(0,0,b'\xff\x51\x03'+us.to_bytes(3,'big')),(0,1,b'\xff\x58\x04\x04\x02\x18\x08'),(0,2,b'\xff\x59\x02\x00\x01')]))
for channel,name,program in [(0,'bass-reconstruction',38),(1,'melody-saw',81),(2,'pad-reconstruction',89)]:
 title=name.encode();events=[(0,0,b'\xff\x03'+vlq(len(title))+title),(0,1,bytes([0xc0+channel,program]))]
 for row in csv.reader((s/f'{name}-notes.csv').open()):
  start,dur,midi,gain=map(float,row);on=round(start*bpm/60*ppq);off=round((start+dur)*bpm/60*ppq)
  note=int(midi);velocity=max(1,min(127,round(gain*110)))
  events.extend([(on,3,bytes([0x90+channel,note,velocity])),(off,2,bytes([0x80+channel,note,0]))])
 tracks.append(chunk(events))
events=[(0,0,b'\xff\x03\x0bGeneric 909')]
for step in range(128):
 for note,hit in [(36,step%4==0),(39,step%8==4),(42,step%2==0),(46,step%4==2)]:
  if hit:events.extend([(step*120,3,bytes([0x99,note,90])),(step*120+50,2,bytes([0x89,note,0]))])
tracks.append(chunk(events))
(s/'estimated-notes.mid').write_bytes(b'MThd'+struct.pack('>IHHH',6,1,len(tracks),ppq)+b''.join(tracks))
(r/'README.txt').write_text(f'{r.name}: local audio separation and Groove study\n\nVocal pack: {len(rows)} PCM24 WAV cuts, original pitch and timing, with timestamp index. Boundaries and lyric labels are automatic; some split held vowels or phrases. Full separation masters are preserved under stems/. Reverb or faint accompaniment can remain.\n\nSynth study: eight bars starting at {a["source_start_seconds"]:.3f} seconds, at {bpm:.3f} BPM. Estimated notes, A-minor chord assumption, generic 909 drums, existing Groove synth voices. MIDI programs are editing placeholders. reference-then-groove.wav plays the backing first, one second of silence, then the synth sketch. Not an exact recreation. See recipe-notes.txt and reconstruction-analysis.json.\n')
for directory,name in [(v,f'{r.name}-vocal-samples'),(s,f'{r.name}-synth-study')]:
 with zipfile.ZipFile(r/f'{name}.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for p in sorted(directory.iterdir()):z.write(p,arcname=name+'/'+p.name)
 with zipfile.ZipFile(r/f'{name}.zip') as z:assert z.testzip() is None
print('Verified',len(rows),'vocal samples and both ZIP archives.')
for p in s.glob('*.wav'):
 x,sr=sf.read(p);assert np.isfinite(x).all() and np.max(abs(x))<1
print('Verified all synth WAVs: 44.1 kHz PCM24, finite, no clipping.')
print([(p.name,round(p.stat().st_size/1024/1024,1)) for p in r.glob('*.zip')])
