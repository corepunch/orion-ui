"""Stage complete vocal phrases with pitch-preserving timing and no interior padding."""
import argparse,hashlib,json,subprocess
from pathlib import Path
import imageio_ffmpeg,numpy as np,soundfile as sf
from scipy.signal import butter,sosfiltfilt
from verify_sample import draw_comparison

SR,BEAT=44100,18900

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def fit_phrase(x,count,ffmpeg,out):
    head,overlap=round(.035*SR),round(.008*SR)
    if len(x)<head*3 or count<head*3:raise ValueError('Phrase is too short')
    rate=(len(x)-head+overlap)/(count-head+overlap)
    if not .5<=rate<=2:raise ValueError(f'Unreviewed extreme stretch rate {rate}')
    # Supply a genuine phrase end followed by silence to flush atempo. Never
    # pad a timing interval inside a word when the filter emits fewer frames.
    body=np.r_[x[head-overlap:],np.zeros(SR)]
    sf.write(out/'stretch-in.wav',body,SR,subtype='FLOAT')
    subprocess.run([ffmpeg,'-v','error','-y','-i',str(out/'stretch-in.wav'),'-af',f'atempo={rate:.10f}','-c:a','pcm_f32le',str(out/'stretch-out.wav')],check=True)
    stretched,hz=sf.read(out/'stretch-out.wav');need=count-head+overlap
    if hz!=SR or len(stretched)<need:raise ValueError('Stretch underflow; do not fill an interior gap')
    y=np.empty(count);y[:head-overlap]=x[:head-overlap];mix=.5-.5*np.cos(np.linspace(0,np.pi,overlap))
    y[head-overlap:head]=x[head-overlap:head]*(1-mix)+stretched[:overlap]*mix;y[head:]=stretched[overlap:need]
    fade=min(round(.025*SR),count//8);y[-fade:]*=.5+.5*np.cos(np.linspace(0,np.pi,fade));y[:44]*=np.linspace(0,1,44)
    return y,rate

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--plan',type=Path,required=True);parser.add_argument('--out-dir',type=Path,required=True);args=parser.parse_args()
    out=args.out_dir.resolve();repo=Path(__file__).resolve().parents[2]
    if out.exists():parser.error('Use a new staging directory')
    out.mkdir(parents=True);ffmpeg=imageio_ffmpeg.get_ffmpeg_exe();decoder=out/'decode_sample'
    subprocess.run(['cc','-std=c99','-O2','-I',str(repo),str(Path(__file__).with_name('decode_sample.c')),'-o',str(decoder)],check=True)
    reports=[]
    for row in json.loads(args.plan.read_text())['samples']:
        source=Path(row['source']);x,hz=sf.read(source,always_2d=True)
        if hz!=SR or not np.isfinite(x).all():raise ValueError('Invalid source')
        x=sosfiltfilt(butter(2,55,fs=SR,btype='highpass',output='sos'),x.mean(axis=1));audio=np.zeros(row['bars']*4*BEAT);regions=[]
        before=[]
        for (a,b),beat in zip(row['source_regions_seconds'],row['target_line_beats']):
            start,end=round(a*SR),round(b*SR);n=round(row['target_active_seconds_per_region']*SR)
            if start<0 or end>len(x) or start>=end:raise ValueError('Region outside isolated source')
            y,rate=fit_phrase(x[start:end],n,ffmpeg,out);target=round(beat*BEAT)
            if target+n>len(audio):raise ValueError('Phrase exceeds musical slot')
            audio[target:target+n]=y;before.append(x[start:end]);regions.append(dict(source_start=a,source_end=b,target_frame=target,target_active_frames=n,atempo_rate=rate))
        peak=float(abs(audio).max())
        if peak<.0005:raise ValueError('Silent source')
        gain=10**(-2/20)/peak;audio*=gain
        name=row['filename'];stem=Path(name).stem;wav=out/(stem+'.wav');mp3=out/name
        sf.write(wav,audio,SR,subtype='PCM_24')
        subprocess.run([ffmpeg,'-v','error','-y','-i',str(wav),'-c:a','libmp3lame','-b:a','320k','-write_xing','1','-id3v2_version','3','-metadata','TBPM=140','-metadata','title='+row['title'],str(mp3)],check=True)
        fmt=list(map(int,subprocess.check_output([str(decoder),str(mp3),str(out/'decoded.pcm')],text=True).split()));decoded=np.fromfile(out/'decoded.pcm',np.int16).astype(float)/32768;signal=np.flatnonzero(abs(decoded)>10**(-55/20));leading=float(signal[0]/SR*1000) if len(signal) else None
        checks=dict(exact_frames=len(decoded)==len(audio),mono_44100=fmt[1:3]==[1,SR],finite_nonzero=bool(np.isfinite(decoded).all() and len(signal)),no_clipping=bool(abs(decoded).max()<1),signal_within_5ms=leading is not None and leading<=5,quiet_endpoints=bool(abs(decoded[[0,-1]]).max()<10**(-55/20)))
        env=np.sqrt(np.mean(decoded[:len(decoded)//220*220].reshape(-1,220)**2,axis=1));active=env>max(.005,float(env.max())*.025)
        report=dict(file=name,title=row['title'],bars=row['bars'],source_sha256=sha(source),raw_take=row['source_take'],source_regions=regions,delivery_sha256=sha(mp3),master_sha256=sha(wav),gain=gain,frames=fmt[0],peak=float(abs(decoded).max()),leading_signal_ms=leading,activity_ratio=float(active.mean()),checks=checks,passed=all(checks.values()))
        reports.append(report)
        sf.write(out/(stem+'-before.wav'),np.concatenate(before)*gain,SR,subtype='FLOAT')
        draw_comparison([('Complete source phrases',np.concatenate(before)*gain,SR),('Final / actual Groove decoder',decoded,SR)],140,BEAT,row['target_line_beats'],[],out/(stem+'-comparison.png'))
        draw_comparison([('Final / actual Groove decoder',decoded,SR)],140,BEAT,row['target_line_beats'],[],out/(stem+'-final.png'))
        (out/'verification.json').write_text(json.dumps(reports,indent=2)+'\n');print(name,checks,'activity',report['activity_ratio'],flush=True)
        if not report['passed']:raise ValueError(f'Export failed checks: {name}')

if __name__=='__main__':main()
