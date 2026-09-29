#!/usr/bin/env python3
"""Copy a portable E2 motion selection and render native frames from an imported library."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET
from ecstatica_convert import source_prefab,reconstruct_shoulders,write_xml,SCALE

SAMPLES=[('joe-run','02500-0000-joe',8),('full-villager-walk','02565-0065-w-vil1',958),
         ('slender-villager-walk','02567-0067-w-vil3',960),('goblin-swing','02509-0009-w-gob1',344)]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('library',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--scener',required=True);p.add_argument('--frames',type=Path,required=True)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True);args.frames.mkdir(parents=True,exist_ok=True)
    inventory=[]
    for name,folder,action in SAMPLES:
        source=args.library/'actors'/folder;report=json.loads((source/'conversion.json').read_text())
        clip=next(c for c in report['clips'] if c['action']==action)
        dest=args.output/name
        if dest.exists():raise ValueError(f'refusing to overwrite {dest}')
        for child in ('prefabs','scenes','actions'):(dest/child).mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/'source.json',dest/'source.json')
        actor=json.loads((source/'source.json').read_text())
        write_xml(dest/'prefabs/source.blk',source_prefab(actor,SCALE,{})[0])
        shutil.copy2(source/clip['file'],dest/'scenes/motion.blks')
        action_file=next((args.library/'actions').glob(f'{action:04d}-*.json'))
        shutil.copy2(action_file,dest/'actions'/action_file.name)
        detail=dict(actor=report['name'],record=report['record'],action=action,
                    source_scene=clip['file'],ignored_events=clip['ignored_events'],
                    unmapped_part_ids=clip['unmapped_part_ids'],timing=clip['timing'],
                    preview_repairs=[p['name'] for p in reconstruct_shoulders(actor)['parts'] if p.get('reconstructed')])
        (dest/'conversion.json').write_text(json.dumps(detail,indent=2)+'\n')
        scene=ET.parse(dest/'scenes/motion.blks');duration=float(scene.find('clip').get('length'))
        output=args.frames/name
        command=[args.scener,'--render',str(dest/'scenes/motion.blks'),'--camera','Standing',
                 '--frames',f'0:{duration}:24','--size','640x640','--format','png','--output-dir',str(output)]
        print('[review] '+name,flush=True)
        result=subprocess.run(command,capture_output=True,text=True)
        (args.frames/(name+'.log')).write_text(result.stdout+result.stderr)
        result.check_returncode()
        frames=sorted(output.glob('Standing_*.png'))
        if len(frames)<2:raise RuntimeError(f'no animation frames for {name}')
        shutil.copy2(frames[len(frames)//2],dest/'poster.png')
        detail|=dict(name=name,frames=len(frames),fps=24,duration=duration)
        inventory.append(detail)
        (dest/'README.md').write_text(f'# {name}\n\nActor `{report["name"]}`, record {report["record"]}, action {action}.\n\n[Scene](scenes/motion.blks) · [Decoded action](actions/{action_file.name}) · [Conversion report](conversion.json)\n\nExperimental decoded motion: inferred transforms, normalized timing, approximate palette. Unsupported commands remain recorded in the conversion report.\n\n![Motion](motion.gif)\n\n[MP4](motion.mp4)\n')
    (args.output/'manifest.json').write_text(json.dumps(inventory,indent=2)+'\n')


if __name__=='__main__':main()
