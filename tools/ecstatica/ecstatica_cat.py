"""Calibrated CAT controls over the preserved imported volume model."""
import copy
import math
import xml.etree.ElementTree as ET
from ecstatica_convert import (SCALE, IDENTITY, source_prefab, reconstruct_shoulders, fields, pose, get, bounds,
    joint_name, fmt, mm, mv, transpose, vadd, vsub, vmul, norm, euler)

BASIS = ((-1,0,0),(0,0,-1),(0,-1,0))


def aim(delta):
    length=norm(delta)
    if length<1e-8:return '0 90'
    return fmt((math.degrees(math.atan2(delta[0],-delta[1])),
                math.degrees(math.asin(max(-1,min(1,delta[2]/length))))))


def calibrated_cat(actor,scale=SCALE,overrides=None):
    actor=reconstruct_shoulders(actor)
    byname={p['name'].lower():p for p in actor['parts']}
    roles={'pelvis':'body','abdomen':'chest','head':'head'}
    for side in ('left','right'):
        for role,part in [('thigh','thigh'),('calf','shin'),('foot','foot'),
                          ('upperarm','upper arm'),('forearm','forearm'),('palm','hand')]:
            roles[f'{side}_{role}']=f'{side} {part}'
    if any(n not in byname for n in roles.values()):return None
    source,state,_,box=source_prefab(actor,scale,overrides or {})
    world=pose(actor,state);lo,hi=box
    shift=((lo[0]+hi[0])*.5*scale,(lo[2]+hi[2])*.5*scale,hi[1]*scale)
    def point(i):return vadd(vmul(mv(BASIS,world[i][1]),scale),shift)
    ids={role:byname[name]['id'] for role,name in roles.items()}
    positions={role:point(i) for role,i in ids.items()}
    for side in ('left','right'):
        for a,b in [('thigh','calf'),('calf','foot'),('upperarm','forearm'),('forearm','palm')]:
            if norm(vsub(positions[f'{side}_{a}'],positions[f'{side}_{b}']))<.001:return None
    chest=ids['abdomen'];cr,co=world[chest]
    # An upper-chest pivot inside the original torso allows the pecs/shoulders to bend separately.
    center=vadd(vmul(mv(BASIS,vadd(co,mv(cr,get(state,chest,'centre')))),scale),shift)
    positions['ribcage']=center
    ids['ribcage']=chest
    root=ET.Element('prefab')
    root.append(ET.Comment('Calibrated CAT rig; original ellipsoid volumes retained. Z up, front -Y, centimetres.'))
    for option in source.findall('option'):root.append(copy.deepcopy(option))
    nodes={};controls={}
    def bone(tag,name,parent,position,endpoint=None,source_id=None,attach=False):
        target=positions.get(endpoint,position) if isinstance(endpoint,str) else endpoint or position
        delta=vsub(target,position)
        node=ET.SubElement(root if parent is None else nodes[parent],tag,
            name=name,aim=aim(delta),length=fmt(norm(delta)),radius='1',overlap='0',volume='0')
        if parent is None:node.set('pos',fmt(position));node.set('ground','0')
        if tag=='limb':node.attrib.pop('length')
        if attach:
            offset=vsub(position,controls[parent]['position'])
            node.set('at','0');node.set('from',aim(offset));node.set('sink',fmt(1-norm(offset)))
        nodes[name]=node
        controls[name]=dict(parent=parent,position=position,source=source_id)
        return node
    bone('hub','pelvis',None,positions['pelvis'],source_id=ids['pelvis'])
    bone('spine','spine','pelvis',positions['pelvis'],'abdomen')
    bone('hub','abdomen','spine',positions['abdomen'],source_id=chest)
    bone('spine','spine_upper','abdomen',positions['abdomen'],'ribcage')
    bone('hub','ribcage','spine_upper',positions['ribcage'],source_id=chest)
    bone('spine','neck','ribcage',positions['ribcage'],'head')
    bone('hub','head','neck',positions['head'],source_id=ids['head'])
    for side in ('left','right'):
        for typ,hub,chain in [('leg','pelvis',('thigh','calf','foot')),('arm','ribcage',('upperarm','forearm','palm'))]:
            names=[f'{side}_{x}' for x in chain];limb=f'{side}_{typ}'
            n=bone('limb',limb,hub,positions[names[0]],attach=True)
            n.set('type',typ);n.set('bend','0 0' if typ=='leg' else '180 0')
            for j,name in enumerate(names):
                tag=('ankle' if typ=='leg' else 'palm') if j==2 else 'bone'
                bone(tag,name,limb if j==0 else names[j-1],positions[name],
                     names[j+1] if j<2 else None,ids[name])
    byid={p['id']:p for p in actor['parts']}
    source_role={i:role for role,i in ids.items() if role!='ribcage'}
    bindings={}
    for p in actor['parts']:
        cursor=p['id']
        while cursor not in source_role and byid[cursor]['parent'] is not None:cursor=byid[cursor]['parent']
        owner=source_role.get(cursor,'pelvis')
        if owner=='abdomen' and p['id']!=chest:owner='ribcage'
        r,o=world[p['id']];wr=mm(BASIS,r)
        local=vsub(point(p['id']),controls[owner]['position'])
        node=ET.SubElement(nodes[owner],'group',name=joint_name(p),pos=fmt(local),rot=fmt(euler(wr)),scale=fmt((scale,)*3))
        original=source.find(f'.//*[@name="{joint_name(p)}"]')
        for shape in original:
            if shape.tag!='group':node.append(copy.deepcopy(shape))
        bindings[p['id']]=dict(owner=owner,rotation=wr,offset=local)
    ET.SubElement(root,'pose',name='Stand')
    bend=ET.SubElement(root,'pose',name='Bend')
    ET.SubElement(bend,'joint',target='spine',rot='15 0 0')
    ET.SubElement(bend,'joint',target='spine_upper',rot='15 0 0')
    turn=ET.SubElement(root,'pose',name='Turn')
    ET.SubElement(turn,'joint',target='spine',rot='0 0 10')
    ET.SubElement(turn,'joint',target='spine_upper',rot='0 0 15')
    ET.SubElement(turn,'joint',target='head',rot='0 0 10')
    for side,sign in [('left',1),('right',-1)]:
        gesture=ET.SubElement(root,'pose',name='Present'+side.title())
        ET.SubElement(gesture,'ik',limb=side+'_arm',offset=fmt((sign*12,-25,26)),bend=fmt((sign*100,-35)))
    calibration=dict(controls=controls,bindings=bindings,shift=shift,scale=scale,
                     source_world=world,height=(hi[1]-lo[1])*scale)
    return root,calibration


def calibrated_clip(actor,action,calibration,duration=1):
    """Preserve the inferred source playback in the calibrated rig's different rest frames."""
    actor=reconstruct_shoulders(actor)
    state=fields(actor);parts={p['id']:p for p in actor['parts']}
    controls=calibration['controls'];bindings=calibration['bindings'];scale=calibration['scale']
    shift=calibration['shift'];rest=calibration['source_world']
    clip=ET.Element('clip',name=f'cat_action_{action["id"]}',length=fmt(duration),ease='linear',loop='0')
    end=max((k['phase'] for k in action['keys']),default=65535) or 65535
    for key in action['keys']:
        for event in key['events']:
            field={1:'rotation',2:'offset',31:'position'}.get(event['opcode'])
            if field and event['index'] in state:state[event['index']][field]=event['values']
        world=pose(actor,state);animated={}
        out=ET.SubElement(clip,'key',t=fmt(key['phase']/end*duration))
        for name,c in controls.items():
            parent=c['parent'];pr,po=animated[parent] if parent else (IDENTITY,(0,0,0))
            bind_offset=vsub(c['position'],controls[parent]['position']) if parent else c['position']
            if c['source'] is None:
                r,o=pr,vadd(po,mv(pr,bind_offset))
            else:
                i=c['source'];sr,so=world[i];rr,ro=rest[i]
                r=mm(mm(mm(BASIS,sr),transpose(rr)),transpose(BASIS))
                source_pos=vadd(vmul(mv(BASIS,so),scale),shift)
                rest_pos=vadd(vmul(mv(BASIS,ro),scale),shift)
                o=vadd(source_pos,mv(r,vsub(c['position'],rest_pos)))
                local_r=mm(transpose(pr),r);local_o=mv(transpose(pr),vsub(o,po))
                ET.SubElement(out,'joint',target=name,rot=fmt(euler(local_r)),pos=fmt(vsub(local_o,bind_offset)))
            animated[name]=(r,o)
        for i,b in bindings.items():
            pr,po=animated[b['owner']];sr,so=world[i]
            r=mm(transpose(pr),mm(BASIS,sr))
            o=mv(transpose(pr),vsub(vadd(vmul(mv(BASIS,so),scale),shift),po))
            delta=mm(transpose(b['rotation']),r)
            offset=vmul(mv(transpose(b['rotation']),vsub(o,b['offset'])),1/scale)
            ET.SubElement(out,'joint',target=joint_name(parts[i]),rot=fmt(euler(delta)),pos=fmt(offset))
    return clip
