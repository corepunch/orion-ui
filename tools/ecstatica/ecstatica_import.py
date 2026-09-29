#!/usr/bin/env python3
"""Import actor libraries and experimental animation previews from Ecstatica I/II ISOs."""
import argparse
import json
import math
from pathlib import Path
import sys
from ecstatica_archive import read_disc, DecodeError
from ecstatica_cat import calibrated_cat, calibrated_clip
from ecstatica_convert import (SCALE, slug, source_prefab, source_clip, cat_prefab, reconstruct_shoulders,
                              study, write_xml, options)


def save_json(path,data):
    path.write_text(json.dumps(data,ensure_ascii=True,indent=2)+'\n')


def import_disc(iso,output,preset,actor_ids=None,animations=True,duration=1,scale=SCALE,overrides=None,cat_animations=False):
    decoded=read_disc(iso)
    game=decoded['game']; folder=Path(output)/game
    if folder.exists() and any(folder.iterdir()):
        raise DecodeError(f'output already contains files: {folder}; choose a fresh output directory')
    actors=decoded.pop('actors');actions=decoded.pop('actions');repertoires=decoded['repertoires']
    if actor_ids is not None:
        available={a['slot'] for a in actors}
        if set(actor_ids)-available:raise DecodeError(f'unknown actor IDs: {sorted(set(actor_ids)-available)}')
        actors=[a for a in actors if a['slot'] in actor_ids]
    folder.mkdir(parents=True,exist_ok=True)
    action_dir=folder/'actions';action_dir.mkdir()
    by_action={a['id']:a for a in actions}
    for action in actions:save_json(action_dir/f'{action["id"]:04d}-{slug(action["name"])}.json',action)
    manifest=dict(schema=1,game=game,version=decoded['version'],source_sha256=decoded['source_sha256'],
                  scale_cm_per_source_unit=scale,animation_duration_seconds=duration,
                  limitations=['Source pose uses an inferred FK/two-link solver, not the game executable.',
                    'Palette RGB is an authored approximation; exact palette indices remain in source.json.',
                    'Animation uses normalized phase and requested duration, not recovered FPS.',
                    'Clips convert ROTATE/OFFSET/POSITION; remaining commands are reported and preserved in action JSON.',
                    'Triangle/texture/zero-axis geometry is retained in source data but not exactly reproduced in previews.',
                    'CAT rigs are neutral preset adaptations, not the original hierarchy or a faithful animation retarget.'],
                  actors=[],actions=len(actions),source_parts=0,clips=0,cat_rigs=0,controlled_rigs=0,controlled_clips=0)
    repmap={r['id']:{e[1] for e in r['entries']} for r in repertoires}
    for number,actor in enumerate(actors):
        key=f'{actor["record"]["record"]:05d}-{actor["slot"]:04d}-{slug(actor["name"])}'
        dest=folder/'actors'/key;dest.mkdir(parents=True)
        save_json(dest/'source.json',actor)
        (dest/'prefabs').mkdir();(dest/'scenes').mkdir()
        prefab,state,local,box=source_prefab(actor,scale,overrides or {})
        height=(box[1][1]-box[0][1])*scale
        write_xml(dest/'prefabs/source.blk',prefab)
        write_xml(dest/'scenes/preview.blks',study('source',height))
        if 'gloves' in options(actor,{}):
            write_xml(dest/'scenes/bare-hands.blks',study('source',height,overrides={'gloves':False}))
        adapted=cat_prefab(actor,preset,scale,overrides or {})
        if adapted:
            cat,mapping=adapted;write_xml(dest/'prefabs/cat.blk',cat)
            write_xml(dest/'scenes/cat-study.blks',study('cat',180))
            if 'gloves' in options(actor,{}):
                write_xml(dest/'scenes/cat-bare-hands.blks',study('cat',180,overrides={'gloves':False}))
            manifest['cat_rigs']+=1
        controlled=calibrated_cat(actor,scale,overrides or {})
        if controlled:
            write_xml(dest/'prefabs/controlled.blk',controlled[0])
            write_xml(dest/'scenes/controlled.blks',study('controlled',height))
            for pose_name in ('Bend','PresentLeft','PresentRight'):
                scene=study('controlled',height);scene.find('prefab').set('pose',pose_name)
                write_xml(dest/'scenes'/f'controlled-{pose_name.lower()}.blks',scene)
            manifest['controlled_rigs']+=1
        rep=next((e['values'][0] for e in actor['events'] if e['opcode']==55),-1)
        ids=sorted(repmap.get(rep,set()));clips=[]
        for aid in ids if animations else []:
            action=by_action.get(aid)
            if not action or not action['keys']:continue
            clip,report=source_clip(actor,action,state,local,duration)
            clips_dir=dest/'scenes/animations';clips_dir.mkdir(exist_ok=True)
            name=f'{aid:04d}-{slug(action["name"])}.blks'
            write_xml(clips_dir/name,study('source',height,clip))
            entry=dict(action=aid,file='scenes/animations/'+name,**report)
            if controlled and cat_animations:
                cat_dir=dest/'scenes/cat-animations';cat_dir.mkdir(exist_ok=True)
                write_xml(cat_dir/name,study('controlled',height,calibrated_clip(actor,action,controlled[1],duration)))
                entry['controlled_file']='scenes/cat-animations/'+name;manifest['controlled_clips']+=1
            clips.append(entry)
        info=dict(id=actor['slot'],name=actor['name'],record=actor['record']['record'],path='actors/'+key,
                  parts=len(actor['parts']),preview_repairs=[p['name'] for p in reconstruct_shoulders(actor)['parts'] if p.get('reconstructed')],cat=bool(adapted),controlled=bool(controlled),features=options(actor,overrides or {}),
                  control_source_mapping={k:v['source'] for k,v in controlled[1]['controls'].items()} if controlled else {},
                  repertoire=rep,action_ids=ids,missing_action_ids=[i for i in ids if i not in by_action],
                  clips=clips,cat_source_mapping=adapted[1] if adapted else {})
        save_json(dest/'conversion.json',info)
        short={k:v for k,v in info.items() if k not in ('clips','cat_source_mapping','control_source_mapping')}
        short['clips']=len(clips);manifest['actors'].append(short)
        manifest['clips']+=len(clips);manifest['source_parts']+=len(actor['parts'])
        if number%100==0:print(f'[ecstatica] {game}: {number+1}/{len(actors)} actors, {manifest["clips"]} clips',file=sys.stderr,flush=True)
    save_json(folder/'manifest.json',manifest)
    save_json(folder/'disc-index.json',decoded)
    lines=[f'# {game} converted library','',f'{len(actors)} actors, {manifest["source_parts"]} parts, {len(actions)} actions, {manifest["clips"]} experimental clips, {manifest["cat_rigs"]} CAT adaptations.','',
           'Open `scenes/cat-study.blks` for a neutral CAT character, `scenes/preview.blks` for the inferred source pose, or an `scenes/animations/*.blks` file for an experimental imported clip.', '',
           '`source.json` preserves original part fields/events. `conversion.json` records feature switches, action associations and unsupported animation commands. `actions/` retains every action, even when no actor repertoire uses it.','',
           'Calibrated CAT controls: open `scenes/controlled.blks` when present to preserve original volumes. Optional `--cat-animations` also exports clips to that rig.', '', '## Conversion limits','']+['- '+x for x in manifest['limitations']]+['','## Actors','','| ID | Record | Name | Parts | CAT | Clips | Features |','|---:|---:|---|---:|---|---:|---|']
    for a in manifest['actors']:
        label=a['name'].replace('|','\\|').replace('[','').replace(']','')
        lines.append(f'| {a["id"]} | {a["record"]} | [{label}]({a["path"]}/scenes/preview.blks) | {a["parts"]} | {"yes" if a["cat"] else "no"} | {a["clips"]} | {", ".join(a["features"])} |')
    (folder/'README.md').write_text('\n'.join(lines)+'\n')
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('isos',type=Path,nargs='+')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--actor',type=int,action='append',help='source actor slot, repeatable; defaults to all')
    parser.add_argument('--no-animations',action='store_true',help='retain action JSON but skip experimental playable scenes')
    parser.add_argument('--cat-animations',action='store_true',help='also export imported clips to calibrated CAT controls')
    parser.add_argument('--duration',type=float,default=1,help='assumed duration of each action in seconds (default 1)')
    parser.add_argument('--scale',type=float,default=SCALE,help='centimetres per raw unit (default .207)')
    parser.add_argument('--feature',action='append',default=[],metavar='NAME=0|1',help='override imported feature defaults')
    parser.add_argument('--preset',type=Path,default=Path(__file__).resolve().parents[2]/'apps/scener/prefabs/characters/presets/biped.blk')
    args=parser.parse_args()
    try:
        if not all(math.isfinite(x) and x>0 for x in (args.duration,args.scale)):
            raise DecodeError('duration and scale must be finite and positive')
        overrides={}
        for text in args.feature:
            name,sep,value=text.partition('=')
            if name not in ('gloves','boots','hair','fingers','effects') or not sep or value not in ('0','1'):
                raise DecodeError(f'invalid feature override: {text}')
            overrides[name]=value=='1'
        for iso in args.isos:
            result=import_disc(iso,args.output,args.preset,args.actor,not args.no_animations,args.duration,args.scale,overrides,args.cat_animations)
            print(json.dumps({k:len(v) if k=='actors' else v for k,v in result.items() if k in ('game','actors','source_parts','actions','clips','cat_rigs','controlled_rigs','controlled_clips')},indent=2))
    except (OSError,ValueError,KeyError,RecursionError) as error:
        print(f'[ecstatica] import failed: {error}',file=sys.stderr)
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
