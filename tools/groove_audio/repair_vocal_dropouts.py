"""Stage source-backed repairs for reviewed padding dropouts in vocal samples."""
import argparse,hashlib,json,subprocess
from pathlib import Path
import imageio_ffmpeg,numpy as np,soundfile as sf
from PIL import Image,ImageDraw
from scipy.signal import butter,sosfiltfilt

SR=44100

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--out-dir',type=Path,required=True)
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[2];out=args.out_dir.resolve()
    if out.exists():parser.error('Use a new staging directory')
    out.mkdir(parents=True);groups={};reports=[];pictures=[]
    for r in json.loads(args.plan.read_text())['repairs']:groups.setdefault(r['file'],[]).append(r)
    for name,patches in groups.items():
        original=repo/'apps/groove/share/vocals'/name
        before,hz=sf.read(repo/patches[0]['master']);audio=before.copy()
        if hz!=SR or audio.ndim!=1:raise ValueError('Invalid master')
        source_path=repo/patches[0]['source']
        if sha(source_path)!=patches[0]['source_sha256']:raise ValueError('Source changed')
        source,hz=sf.read(source_path,always_2d=True)
        if hz!=SR:raise ValueError('Invalid source rate')
        source=sosfiltfilt(butter(2,55,fs=SR,btype='highpass',output='sos'),source.mean(axis=1))*patches[0]['source_gain']
        evidence=[]
        for p in patches:
            a,b=p['patch_start_frame'],p['patch_end_frame'];anchor=p['anchor_frame'];s=round(p['source_anchor_seconds']*SR)+(a-anchor);n=b-a
            if a<0 or b>len(audio) or s<0 or s+n>len(source):raise ValueError('Patch outside authentic source')
            weights=np.ones(n);left,right=p['crossfade_in_frames'],p['crossfade_out_frames']
            weights[:left]=.5-.5*np.cos(np.linspace(0,np.pi,left));weights[-right:]=.5+.5*np.cos(np.linspace(0,np.pi,right))
            audio[a:b]=audio[a:b]*(1-weights)+source[s:s+n]*weights
            ga,gb=p['gap_start'],p['gap_end'];old=np.sqrt(np.mean(before[ga:gb]**2));new=np.sqrt(np.mean(audio[ga:gb]**2))
            evidence.append(dict(start_frame=a,end_frame=b,source_start_frame=s,gap_start_frame=ga,gap_end_frame=gb,before_gap_rms=float(old),after_gap_rms=float(new)))
            im=Image.new('RGB',(1500,240),'#14202b');d=ImageDraw.Draw(im);d.text((10,7),f'{name}: dropout at {ga/SR:.6f}s ({p["duration_ms"]:.2f} ms original padding)',fill='white')
            lo=max(0,a-441);hi=min(len(audio),b+441)
            for j,(x,label,col) in enumerate([(before,'Before','#dc8d91'),(audio,'Repaired','#6dd9ba')]):
                mid=75+j*105;d.text((10,mid-20),label,fill=col)
                for k,v in enumerate(np.array_split(x[lo:hi],1300)):d.line((180+k,mid-max(v)*75,180+k,mid-min(v)*75),fill=col)
            pictures.append(im)
        wav=out/(Path(name).stem+'.wav');sf.write(wav,audio,SR,subtype='PCM_24')
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-y','-i',str(wav),'-i',str(original),'-map','0:a:0','-map_metadata','1','-c:a','libmp3lame','-b:a','320k','-write_xing','1','-id3v2_version','3','-metadata','TBPM=140',str(out/name)],check=True)
        unchanged=np.ones(len(audio),bool)
        for p in patches:unchanged[p['patch_start_frame']:p['patch_end_frame']]=False
        reports.append(dict(file=name,original_sha256=sha(original),new_sha256=sha(out/name),frames=len(audio),patches=evidence,unchanged_pcm_outside_patches=bool(np.array_equal(before[unchanged],audio[unchanged])),peak=float(abs(audio).max())))
        print(name,len(patches),'repairs',flush=True)
    (out/'repairs.json').write_text(json.dumps(reports,indent=2)+'\n')
    for i in range(0,len(pictures),6):
        im=Image.new('RGB',(1500,240*len(pictures[i:i+6])))
        for j,v in enumerate(pictures[i:i+6]):im.paste(v,(0,j*240))
        im.save(out/f'repair-details-{i//6+1}.png')

if __name__=='__main__':main()
