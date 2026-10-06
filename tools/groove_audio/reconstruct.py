from pathlib import Path
import json, subprocess
import numpy as np
import librosa
import soundfile as sf
from scipy.ndimage import median_filter

from common import prepare_work, work_arguments
parser = work_arguments('Estimate and render an eight-bar A-minor Groove sketch.')
args = parser.parse_args()
root = prepare_work(args)
out = root/'synth-study'
out.mkdir(exist_ok=True)
build = root/'build'
renderer = build/'render_study'
inst = json.loads((build/'instrument-index.json').read_text())
excerpt = json.loads((root/'instrument-study/excerpt.json').read_text())
source_start = excerpt['source_start_seconds']
source_end = source_start + excerpt['duration_seconds']
beats = np.array(json.loads((root/'track-analysis.json').read_text())['beat_times'])
selected = beats[(beats>=source_start)&(beats<source_end)]
if len(selected) < 2:
    raise ValueError('Need at least two detected beats in the study excerpt')
origin = float(selected[0])
bpm = float(60/np.polyfit(np.arange(len(selected)),selected,1)[0])
beat = 60/bpm
duration = 32*beat
offset = origin-source_start
if origin + duration > source_end:
    raise ValueError('Excerpt is too short for eight bars at the estimated local tempo')
sr = 22050
hop = 256
tracks = {}
for name in ['bass','melodic']:
    y,s = sf.read(root/'instrument-study'/f'{name}.wav',dtype='float32',always_2d=True)
    y = librosa.resample(y.mean(axis=1),orig_sr=s,target_sr=sr)
    tracks[name] = y[round(offset*sr):round((offset+duration)*sr)]
    if len(tracks[name]) < round(duration*sr)-1:
        raise ValueError(f'{name} stem is shorter than the excerpt metadata')

def pitch_events(y, low, high, steps_per_beat, bass=False):
    spectrum=np.abs(librosa.stft(y,n_fft=4096,hop_length=hop))
    f=np.fft.rfftfreq(4096,1/sr)
    notes=np.arange(low,high+1)
    templates=[]
    for midi in notes:
        hz=librosa.midi_to_hz(midi)
        template=np.zeros(len(f))
        for h in range(1,9):
            template+=np.exp(-.5*((f-hz*h)/7)**2)/(h**1.4)
        template/=max(np.linalg.norm(template),1e-9)
        templates.append(template)
    scores=np.array(templates)@spectrum
    # Prefer evidence of a fundamental over a lower subharmonic explanation.
    for k,m in enumerate(notes):
        idx=round(librosa.midi_to_hz(m)/sr*4096)
        scores[k]*=(.35+.65*np.minimum(1,spectrum[idx]/np.maximum(scores[k]*.5,1e-8)))
    predicted=median_filter(notes[np.argmax(scores,axis=0)],size=5)
    activity=np.sqrt(np.mean(spectrum*spectrum,axis=0))
    unit=beat/steps_per_beat
    sequence=[]
    for step in range(32*steps_per_beat):
        a=round((step*unit+.015)*sr/hop)
        b=round(((step+1)*unit-.015)*sr/hop)
        if b<=a:b=a+1
        p=predicted[a:b]
        r=activity[a:b]
        if len(p)==0 or np.mean(r)<np.percentile(activity,85)*.10:
            sequence.append(None);continue
        sequence.append(int(np.bincount(p).argmax()))
    events=[]
    for step,midi in enumerate(sequence):
        if midi is None:continue
        if not bass and step>0 and sequence[step-1]==midi:
            events[-1][1]+=unit
        else:
            events.append([step*unit,unit*(.7 if bass else .8),midi,.65])
    return sequence,events

bass_seq,bass_events=pitch_events(tracks['bass'],33,57,2,True)
lead_seq,lead_events=pitch_events(tracks['melodic'],60,84,2)

def render(name,events,voice,fx=0):
    csv=out/f'{name}-notes.csv'
    csv.write_text(''.join(','.join(str(v) for v in row)+'\n' for row in events))
    raw=build/f'{name}.f32'
    subprocess.run([str(renderer),str(csv),str(inst[voice]),str(fx),str(duration),str(raw),str(bpm)],check=True)
    x=np.fromfile(raw,dtype='float32').astype('float64')
    assert np.isfinite(x).all() and np.max(np.abs(x))>0
    x*=min(.8/np.max(np.abs(x)),.17/max(np.sqrt(np.mean(x*x)),1e-8))
    fade=round(.008*44100)
    x[:fade]*=np.linspace(0,1,fade);x[-fade:]*=np.linspace(1,0,fade)
    sf.write(out/f'{name}.wav',x,44100,subtype='PCM_24')
    return x

bass=render('bass-reconstruction',bass_events,'I_MOOG')
lead=render('melody-saw',lead_events,'I_SAW',1<<4)
piano=render('melody-house-piano',lead_events,'I_HPIANO',1<<4)
pluck=render('melody-pluck',lead_events,'I_PLUCK',1<<4)

chroma=librosa.feature.chroma_stft(y=tracks['melodic'],sr=sr,n_fft=4096,hop_length=hop)
chords={
    'Am':[9,0,4],'F':[5,9,0],'C':[0,4,7],'G':[7,11,2],
    'Dm':[2,5,9],'Em':[4,7,11], 'Bdim':[11,2,5],
}
chord_events=[]; progression=[]
for bar in range(8):
    a=round(bar*4*beat*sr/hop);b=round((bar+1)*4*beat*sr/hop)
    profile=chroma[:,a:b].mean(axis=1)
    chord=max(chords,key=lambda c:profile[chords[c]].sum())
    progression.append(chord)
    for pc in chords[chord]:chord_events.append([bar*4*beat,3.7*beat,60+pc,.3])
pad=render('pad-reconstruction',chord_events,'I_PAD',1<<5)

drum_raw=build/'drums.f32'
subprocess.run([str(renderer),'--drums',str(duration),str(drum_raw),str(bpm)],check=True)
drums=np.fromfile(drum_raw,dtype='float32').astype('float64')
drums*=min(.98/np.max(np.abs(drums)),.18/max(np.sqrt(np.mean(drums*drums)),1e-8))
sf.write(out/'drums-reconstruction.wav',drums,44100,subtype='PCM_24')
mix=drums*.8+bass*.9+lead*.7+pad*.25
mix*=.89/np.max(np.abs(mix))
sf.write(out/'groove-reconstruction.wav',mix,44100,subtype='PCM_24')
reference,s=sf.read(root/'stems/instrumental.wav',start=round(origin*44100),frames=len(mix),dtype='float32')
sf.write(out/'reference-backing.wav',reference,44100,subtype='PCM_24')
ref=reference.mean(axis=1);ref*=.89/max(np.max(np.abs(ref)),1e-8)
comparison=np.r_[ref,np.zeros(44100),mix]
sf.write(out/'reference-then-groove.wav',comparison,44100,subtype='PCM_24')

def note_name(m):return '-' if m is None else librosa.midi_to_note(m,unicode=False)
metadata={'source_start_seconds':origin,'study_bpm':bpm,'duration_seconds':duration,
          'key_assumption':'A minor',
          'estimated_bass_eighth_notes':[note_name(m) for m in bass_seq],
          'estimated_dominant_melody_eighth_notes':[note_name(m) for m in lead_seq],
          'estimated_chords_per_bar':progression,
          'method':'Harmonic-template pitch estimates from separated bass and melodic stems. Chords chosen from A-minor diatonic triads. Drum pattern is a generic 909-style reconstruction, not a drum transcription.',
          'limits':'Polyphonic melody and chords are estimates; octave errors and chord-tone substitutions are possible. Voice patches and effects are unchanged Groove defaults. This is a feasibility sketch, not a note-perfect or timbre-matched recreation.'}
(out/'reconstruction-analysis.json').write_text(json.dumps(metadata,indent=2))
print(json.dumps(metadata,indent=2),flush=True)

# Fade only at render time so packaging does not alter audio on repeat runs.
for name in ['drums-reconstruction','groove-reconstruction','reference-backing','reference-then-groove']:
    path=out/f'{name}.wav'
    x,rate=sf.read(path,always_2d=True)
    fade=min(round(.008*rate),len(x)//2)
    x[:fade]*=np.linspace(0,1,fade)[:,None]
    x[-fade:]*=np.linspace(1,0,fade)[:,None]
    if name=='reference-then-groove':
        boundary=len(mix)
        x[boundary-fade:boundary]*=np.linspace(1,0,fade)[:,None]
    sf.write(path,x,rate,subtype='PCM_24')
