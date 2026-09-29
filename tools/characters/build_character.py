#!/usr/bin/env python3
"""Source-preserving Ecstatica II characters with coordinated skeleton/volume morphs."""
import argparse
from functools import lru_cache
import json
import math
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO/'tools/ecstatica'))
from ecstatica_convert import (fmt,write_xml,study,fields,pose,get,mm,mv,transpose,vadd,vsub,vmul,reconstruct_shoulders,
    norm,rotation,euler,orient_z,joint_name,SCALE,IDENTITY)
from ecstatica_cat import calibrated_cat,aim,BASIS

REFERENCES=Path(__file__).with_name('references')
BASES={'joe':'Athletic · Joe','villager-full':'Fuller villager · w_vil1',
       'villager-belly':'Round-bellied villager · w_vil2','villager-slim':'Slender villager · w_vil3',
       'freegirl':'Slender woman · freegirl'}
DEFAULTS=dict(name='Joe',base='joe',height_heads=None,frame=1,fullness=0,muscle=0,shoulders=1,head_size=1,youth=0,posture=0,
              gloves=True,boots=True,hair=True)
RANGES=dict(height_heads=(3,9),frame=(.7,1.6),fullness=(-1,1),muscle=(-1,1),shoulders=(.7,1.3),head_size=(.75,1.4),youth=(0,1),posture=(-1,1))
BONES={'bone','hub','spine','limb','palm','ankle'}


def validate(recipe):
    if not isinstance(recipe,dict):raise ValueError('recipe must be an object')
    if set(recipe)-set(DEFAULTS):raise ValueError('Unsupported fields; old synthetic recipes are not compatible: '+', '.join(sorted(set(recipe)-set(DEFAULTS))))
    spec=DEFAULTS|recipe
    if not isinstance(spec['name'],str) or not spec['name'].strip():raise ValueError('name must be nonempty text')
    if not isinstance(spec['base'],str) or spec['base'] not in BASES:raise ValueError('unknown source character')
    if spec['height_heads'] is None:spec['height_heads']=source_metrics(spec['base'])['heads']
    for key,(low,high) in RANGES.items():
        v=spec[key]
        if type(v) not in (int,float) or not math.isfinite(v) or not low<=v<=high:raise ValueError(f'{key} must be between {low} and {high}')
    for key in ('gloves','boots','hair'):
        if type(spec[key]) is not bool:raise ValueError(f'{key} must be boolean')
    return spec


def reference(key):return json.loads((REFERENCES/(key+'.json')).read_text())
def diagonal(x,y,z):return ((x,0,0),(0,y,0),(0,0,z))
def rx(degrees):return rotation((degrees*65536/360,0,0))
def values(node,key):return tuple(map(float,node.get(key).split()))


def axis_ratio(parts,name,axis,default=1):
    a,b=parts
    if name not in a or name not in b:return default
    old=a[name]['fields']['halfaxes']['values'][axis];new=b[name]['fields']['halfaxes']['values'][axis]
    return new/old if old and new else default


@lru_cache(maxsize=5)
def source_metrics(base):
    actor=reference(base);_,cal=calibrated_cat(actor);state=fields(actor)
    feet=[];tops=[];bottoms=[]
    for part in actor['parts']:
        b=cal['bindings'][part['id']];owner=b['owner'];r=b['rotation']
        center=vadd(cal['controls'][owner]['position'],vadd(b['offset'],vmul(mv(r,get(state,part['id'],'centre')),SCALE)))
        axes=vmul(get(state,part['id'],'halfaxes'),SCALE)
        extent=math.sqrt(sum((r[2][j]*axes[j])**2 for j in range(3)))
        if owner.endswith('_foot'):feet.append(center[2]-extent)
        if owner=='head':tops.append(center[2]+extent)
        if owner=='head' and part['name'].lower()!='neck':bottoms.append(center[2]-extent)
    floor=min(feet);top=max(tops);head_unit=top-min(bottoms);joint=cal['controls']['head']['position'][2]
    foot_joint=min(cal['controls'][side+'_foot']['position'][2] for side in ('left','right'))
    return dict(heads=(top-floor)/head_unit,unit=head_unit,floor=floor,joint=joint,foot_joint=foot_joint,top_offset=top-joint)


def appearance(recipe):
    spec=validate(recipe);actor=reconstruct_shoulders(reference(spec['base']));root,cal=calibrated_cat(actor,overrides={k:spec[k] for k in ('gloves','boots','hair')})
    for node in root.findall('option'):
        if node.get('name') in ('fingers','effects'):node.set('enabled','0')
    sources={k:reference(k) for k in ('joe','villager-full','villager-belly','villager-slim')}
    byname={k:{p['name'].lower():p for p in a['parts']} for k,a in sources.items()}
    full=spec['fullness'];young=spec['youth'];size=1;frame=spec['frame']
    metrics=source_metrics(spec['base'])
    body_length=(spec['height_heads']*metrics['unit']-metrics['top_offset']-(metrics['foot_joint']-metrics['floor']))/(metrics['joint']-metrics['foot_joint'])
    direction='villager-full' if full>=0 else 'villager-slim';pair=(byname['joe'],byname[direction])
    ratio=lambda name,axis:1+abs(full)*(axis_ratio(pair,name,axis)-1)
    chest_x=ratio('chest',0)*spec['shoulders']*(1-.18*young)
    chest_y=ratio('chest',2)
    hip_x=1+abs(full)*(max(1,axis_ratio((byname['joe'],byname['villager-full']),'body',0))-1)*(1 if full>=0 else -.65)
    hip_y=1+abs(full)*(axis_ratio((byname['joe'],byname['villager-full']),'body',2)-1)*(1 if full>=0 else -.65)
    leg_length=1-.28*young;arm_length=1-.22*young;torso_length=1-.12*young
    head_scale=spec['head_size']*(1+.38*young)
    extremity_scale=(1-.12*young)*(1+.12*full)
    def limb_girth(part):
        measured={'upperarm':.32,'forearm':.30,'thigh':.27,'calf':.22}[part]
        return (1+measured*full)*(1-.12*young)
    controls=cal['controls'];positions={};turns={};stretch={}
    def deformation(role):
        if role=='head':return diagonal(head_scale*(1+.08*full),head_scale*(1+.06*full),head_scale)
        if role in ('pelvis','spine'):return diagonal(hip_x,hip_y,1)
        if role in ('abdomen','spine_upper','ribcage','neck'):return diagonal(chest_x,chest_y,torso_length)
        if role.endswith(('_thigh','_calf','_upperarm','_forearm')):
            side=role.split('_')[0];part=role[len(side)+1:]
            next_role=side+'_'+{'thigh':'calf','calf':'foot','upperarm':'forearm','forearm':'palm'}[part]
            axis=vsub(controls[next_role]['position'],controls[role]['position'])
            basis=orient_z(axis,(1,0,0));length=leg_length if part in ('thigh','calf') else arm_length
            girth=limb_girth(part)
            return mm(mm(basis,diagonal(girth,girth,length)),transpose(basis))
        return diagonal(extremity_scale,extremity_scale,extremity_scale)
    for name,c in controls.items():
        parent=c['parent'];base_pos=c['position'];pr=turns[parent] if parent else IDENTITY
        bend=spec['posture']*(.25 if spec['posture']<0 else 1)
        own_turn=rx(bend*(6 if name=='spine' else 18 if name=='spine_upper' else 0))
        turns[name]=mm(pr,own_turn);stretch[name]=deformation(name)
        if parent is None:positions[name]=base_pos;continue
        delta=vsub(base_pos,controls[parent]['position'])
        if name.endswith('_arm'):
            delta=(delta[0]*chest_x,delta[1],delta[2]*torso_length)
        elif name.endswith('_leg'):delta=(delta[0]*hip_x,delta[1]*hip_y,delta[2])
        elif parent.endswith(('_thigh','_calf')):delta=vmul(delta,leg_length)
        elif parent.endswith(('_upperarm','_forearm')):delta=vmul(delta,arm_length)
        elif name=='head':delta=vmul(delta,1-.32*young)
        elif name in ('abdomen','ribcage'):delta=vmul(delta,torso_length)
        positions[name]=vadd(positions[parent],mv(pr,delta))
    anchor=controls['pelvis']['position']
    positions={k:(anchor[0]+(v[0]-anchor[0])*frame,anchor[1]+(v[1]-anchor[1])*frame,metrics['floor']+(v[2]-metrics['floor'])*body_length) for k,v in positions.items()}
    def height_transform(owner):return diagonal(1 if owner=='head' else frame,1 if owner=='head' else frame,1 if owner=='head' or owner.endswith(('_palm','_foot')) else body_length)
    def body_transform(owner):return mm(height_transform(owner),mm(turns[owner],stretch[owner]))
    # Keep the source sole plane while changing leg length and foot/boot fullness.
    source_floor=[];edited_floor=[];state=fields(actor)
    for part in actor['parts']:
        binding=cal['bindings'][part['id']];owner=binding['owner']
        if not owner.endswith('_foot'):continue
        r=binding['rotation'];axes=vmul(get(state,part['id'],'halfaxes'),SCALE)
        center=vadd(binding['offset'],vmul(mv(r,get(state,part['id'],'centre')),SCALE))
        transform=body_transform(owner);edited_rotation=mm(transform,r)
        extent=lambda matrix:math.sqrt(sum((matrix[2][j]*axes[j])**2 for j in range(3)))
        source_floor.append(controls[owner]['position'][2]+center[2]-extent(r))
        edited_floor.append(positions[owner][2]+mv(transform,center)[2]-extent(edited_rotation))
    shift=min(source_floor)-min(edited_floor) if source_floor else 0
    positions={k:vadd(v,(0,0,shift)) for k,v in positions.items()}
    nodes={n.get('name'):n for n in root.iter() if n.tag in BONES}
    for name,node in nodes.items():
        parent=controls[name]['parent']
        if parent is None:node.set('pos',fmt(vmul(positions[name],size)))
        elif node.get('at') is not None:
            delta=vmul(vsub(positions[name],positions[parent]),size)
            node.set('from',aim(delta));node.set('sink',fmt(size-norm(delta)))
        children=[c for c in node if c.tag in BONES and c.get('at') is None]
        if children:
            endpoint=positions[children[0].get('name')];delta=vmul(vsub(endpoint,positions[name]),size)
            if node.tag!='limb':node.set('length',fmt(norm(delta)));node.set('aim',aim(delta))
        node.set('radius',fmt(size))
    parts={p['id']:p for p in actor['parts']};state=fields(actor)
    # Keep the imported geometry, including all face pieces. Morph in each control's body-aligned frame.
    for ident,binding in cal['bindings'].items():
        owner=binding['owner'];node=root.find(f'.//group[@name="{joint_name(parts[ident])}"]')
        local=binding['offset'];r=binding['rotation'];outer=height_transform(owner)
        if parts[ident]['name'].lower()=='neck':outer=diagonal(frame,frame,1)
        transform=mm(outer,mm(turns[owner],stretch[owner]))
        center=vadd(local,vmul(mv(r,get(state,ident,'centre')),SCALE))
        node.set('pos',fmt(vmul(mv(transform,center),size)));node.attrib.pop('rot',None);node.attrib.pop('scale',None)
        shapes=list(node)
        if not shapes:continue
        height_group=ET.SubElement(node,'group',scale=fmt((outer[0][0],outer[1][1],outer[2][2])))
        target=ET.SubElement(height_group,'group',rot=fmt(euler(turns[owner])),scale=fmt((size,)*3))
        if owner.endswith(('_thigh','_calf','_upperarm','_forearm')):
            side=owner.split('_')[0];part=owner[len(side)+1:]
            nxt=side+'_'+{'thigh':'calf','calf':'foot','upperarm':'forearm','forearm':'palm'}[part]
            basis=orient_z(vsub(controls[nxt]['position'],controls[owner]['position']),(1,0,0))
            length=leg_length if part in ('thigh','calf') else arm_length;girth=limb_girth(part)
            target=ET.SubElement(target,'group',rot=fmt(euler(basis)))
            target=ET.SubElement(target,'group',scale=fmt((girth,girth,length)))
            target=ET.SubElement(target,'group',rot=fmt(euler(transpose(basis))))
        else:target=ET.SubElement(target,'group',scale=fmt((stretch[owner][0][0],stretch[owner][1][1],stretch[owner][2][2])))
        target=ET.SubElement(target,'group',rot=fmt(euler(r)))
        label=parts[ident]['name'].lower();muscle=spec['muscle']
        muscular=any(x in label for x in ('shoulder','pectoral','upper arm','forearm','thigh','calf','back'))
        for shape in shapes:
            node.remove(shape);shape.set('pos','0 0 0')
            axes=tuple(abs(v)*SCALE for v in get(state,ident,'halfaxes'))
            if label in ('cheeks','neck'):
                # Skull growth is slight; measured cheek and neck differences carry facial fullness.
                width=axis_ratio(pair,label,0)-1
                if full<0:width=min(width,-.12 if label=='cheeks' else -.25)
                desired=1+abs(full)*width
                axes=(axes[0]*desired/(1+.08*full),axes[1],axes[2]*(1+.12*full))
            if muscular:
                thin=byname['villager-slim'].get(label);athletic=byname['joe'].get(label)
                if thin and athletic:
                    a=athletic['fields']['halfaxes']['values'];t=thin['fields']['halfaxes']['values']
                    longest=max(range(3),key=lambda j:a[j]);ratios=[min(1,t[j]/a[j]) if a[j] else 1 for j in range(3)]
                    ratios[longest]=1
                else:ratios=[.62,.62,.62]
                # These parts also cover the arm root and define the chest surface.
                # Missing parts on another actor are not a valid zero-muscle target.
                if 'shoulder' in label or 'pectoral' in label:ratios=[max(.92,v) for v in ratios]
                axes=tuple(v*(1+abs(muscle)*(ratios[j]-1)*(1 if muscle<0 else -1)) for j,v in enumerate(axes))
            shape.set('radii',fmt(tuple(max(v,.35*SCALE) for v in axes)));target.append(shape)
    # A real belly primitive from w_vil2, not an invented overlay. It emerges only as fullness increases.
    if full>0 and not any(p['name'].lower()=='frame' for p in actor['parts']):
        belly_actor=sources['villager-belly'];br,bc=calibrated_cat(belly_actor)
        p=next(p for p in belly_actor['parts'] if p['name'].lower()=='frame');b=bc['bindings'][p['id']]
        owner='ribcage';r=b['rotation'];bs=fields(belly_actor)
        center=vadd(b['offset'],vmul(mv(r,get(bs,p['id'],'centre')),SCALE))
        center=mv(mm(height_transform(owner),turns[owner]),center)
        container=ET.SubElement(nodes[owner],'group',name='source_villager_belly',pos=fmt(center),scale=fmt((frame,frame,body_length)))
        container=ET.SubElement(container,'group',rot=fmt(euler(mm(turns[owner],r))))
        color=next(n.get('color') for n in root.find('.//group[@name="p1_chest"]').iter('ellipsoid'))
        ET.SubElement(container,'ellipsoid',radii=fmt(vmul(get(bs,p['id'],'halfaxes'),SCALE*full)),color=color,rings='10',slices='16')
    for pose_node in root.findall('pose'):
        for ik in pose_node.findall('ik'):
            side=ik.get('limb').split('_')[0]
            shoulder=positions[side+'_upperarm'];elbow=positions[side+'_forearm'];hand=positions[side+'_palm']
            upper=norm(vsub(elbow,shoulder));lower=norm(vsub(hand,elbow))
            reach=max(.72*(upper+lower),abs(upper-lower)+.01)
            direction=(.35 if side=='left' else -.35,-.75,-.3)
            direction=mv(turns[side+'_upperarm'],vmul(direction,1/norm(direction)))
            goal=vadd(shoulder,vmul(direction,reach))
            ik.set('offset',fmt(vmul(vsub(goal,hand),size)))
    spec['name']=spec['name'].strip()
    return root,spec


def scene_for(root,pose_name=None,base='joe'):
    scene=study('character',180);scene.set('duration','4');inst=scene.find('prefab');inst.set('name','Character')
    if pose_name:inst.set('pose',pose_name)
    # Fixed framing is deliberate: height changes must remain visible.
    for camera in scene.findall('camera'):
        camera.set('fov','35')
        if camera.get('name')=='Standing':camera.set('pos','-220 -440 180');camera.set('look','0 0 115')
        if camera.get('name')=='Front':camera.set('pos','0 -490 105');camera.set('look','0 0 115')
        if camera.get('name')=='Side':camera.set('pos','-490 0 105');camera.set('look','0 0 115')
    actor=reference(base);_,cal=calibrated_cat(actor)
    part=next(p for p in actor['parts'] if p['name'].lower()=='head');binding=cal['bindings'][part['id']]
    center=vadd(cal['controls']['head']['position'],vadd(binding['offset'],vmul(mv(binding['rotation'],part['fields']['centre']['values']),SCALE)))
    ET.SubElement(scene,'camera',name='Portrait',pos=fmt(vadd(center,(-25,-100,15))),look=fmt(center),fov='30')
    return scene


def build(recipe,output):
    root,spec=appearance(recipe);output=Path(output)
    if output.exists() and any(output.iterdir()):raise ValueError('output is not empty: '+str(output))
    (output/'prefabs').mkdir(parents=True,exist_ok=True);(output/'scenes').mkdir()
    write_xml(output/'prefabs/character.blk',root);(output/'recipe.json').write_text(json.dumps(spec,indent=2)+'\n')
    for name,pose_name in [('study',None),('bend','Bend'),('present','PresentLeft')]:write_xml(output/'scenes'/f'{name}.blks',scene_for(root,pose_name,spec['base']))
    scene=scene_for(root,base=spec['base']);ET.SubElement(scene.find('prefab'),'gait',mode='spot',stride='24',lift='5',speed='36',footRoll='0',armSwing='6',armBend='8')
    write_xml(output/'scenes/walk.blks',scene)
    (output/'README.md').write_text(f'# {spec["name"]}\n\nBase: {BASES[spec["base"]]}. Decoded source face and body volumes; CAT controls.\n\n[Study](scenes/study.blks) · [Bend](scenes/bend.blks) · [Present](scenes/present.blks) · [Walk](scenes/walk.blks)\n')
    print(f'[character] built {spec["name"]}: {output}',file=sys.stderr,flush=True)
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('recipe',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:build(json.loads(args.recipe.read_text()),args.output)
    except (OSError,ValueError,TypeError) as error:print('[character] '+str(error),file=sys.stderr);return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
