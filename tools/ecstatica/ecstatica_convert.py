"""Scener export of decoded actors. Geometry is exact in local halfaxes;
poses/colour/animation timing are explicitly experimental approximations.
"""
import copy
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

SCALE = .207
IDENTITY = ((1,0,0),(0,1,0),(0,0,1))
PALETTE = [(0.03,.03,.035),(.07,.055,.04),(.40,.22,.09),(.92,.63,.35),(.15,.25,.6),(.4,.25,.6),(.5,.025,.02),(.9,.88,.75),(.2,.24,.22),(.5,.3,.1),(.33,.4,.035),(.2,.45,.4),(.72,.025,.05),(.65,.035,.04),(.75,.7,.25),(.7,.7,.7)]


def slug(name):
    return re.sub('[^a-z0-9]+','-',name.lower()).strip('-')[:64] or 'unnamed'


def vadd(a,b): return tuple(x+y for x,y in zip(a,b))
def vsub(a,b): return tuple(x-y for x,y in zip(a,b))
def vmul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(dot(a,a))
def unit(a): return vmul(a,1/max(norm(a),1e-9))
def transpose(a): return tuple(zip(*a))
def mv(a,v): return tuple(dot(row,v) for row in a)
def mm(a,b): return tuple(tuple(dot(row,col) for col in zip(*b)) for row in a)


def rotation(words):
    x,y,z=(v*2*math.pi/65536 for v in words)
    cx,sx,cy,sy,cz,sz=math.cos(x),math.sin(x),math.cos(y),math.sin(y),math.cos(z),math.sin(z)
    return ((cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx),
            (sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx),(-sy,cy*sx,cy*cx))


def euler(m):
    y=math.asin(max(-1,min(1,-m[2][0])))
    if abs(math.cos(y))>1e-7:
        x,z=math.atan2(m[2][1],m[2][2]),math.atan2(m[1][0],m[0][0])
    else:
        x,z=0,math.atan2(-m[0][1],m[1][1])
    return tuple(math.degrees(v) for v in (x,y,z))


def orient_z(direction, reference):
    z=unit(direction)
    x=unit(vsub(reference,vmul(z,dot(reference,z))))
    if norm(x)<.1: x=unit(cross((0,1,0),z))
    if norm(x)<.1: x=unit(cross((1,0,0),z))
    y=unit(cross(z,x))
    return transpose((x,y,z))


def fmt(v):
    if isinstance(v,(int,float)):return f'{v:.7g}'
    return ' '.join(fmt(x) for x in v)


def fields(actor):
    return {p['id']:{k:f['values'][:] for k,f in p['fields'].items()} for p in actor['parts']}


def get(state,p,key,default=(0,0,0)):return state.get(p,{}).get(key,default)


def joint_name(p):return f'p{p["id"]}_{slug(p["name"]).replace("-","_")}'[:63]


def feature(name):
    name=name.lower()
    if name in ('left bb','right bb','h_hold'):return 'effects'
    if 'glove' in name:return 'gloves'
    if 'boot' in name:return 'boots'
    if name in ('hair','toupee','bob','ponytail','hair left','hair right'):return 'hair'
    if 'finger' in name or 'thumb' in name or name.endswith(' fing'):return 'fingers'
    return None


def pose(actor,state):
    """Experimental FK plus target-driven two-link limbs; never called a game emulator."""
    parts={p['id']:p for p in actor['parts']}
    children={i:[] for i in parts}
    roots=[]
    for p in parts.values():
        (roots if p['parent'] is None else children[p['parent']]).append(p['id'])
    world, overrides={},{}
    def visit(i):
        p=parts[i]; par=p['parent']; flags=get(state,i,'flags_command')[0]
        turn=rotation(get(state,i,'rotation'))
        if par is None:
            origin=tuple(get(state,i,'offset'))
        else:
            pr,po=world[par]
            origin=vadd(po,mv(pr,vadd(get(state,par,'centre'),get(state,i,'offset'))))
            if not flags & 0x2000: turn=mm(pr,turn)
        if i in overrides:turn,origin=overrides[i]
        world[i]=(turn,origin)
        if flags & 0x20 and not i in overrides:
            mids=[j for j in children[i] if not get(state,j,'flags_command')[0]&0x1000 and 'halfaxes' in state[j]]
            if mids:
                mid=mids[0]
                tips=[j for j in children[mid] if get(state,j,'flags_command')[0]&0x2000]
                if tips:
                    tip=tips[0];target=tuple(get(state,tip,'position'))
                    l1=abs(get(state,i,'centre')[2]+get(state,mid,'offset')[2])
                    l2=abs(get(state,mid,'centre')[2]+get(state,tip,'offset')[2])
                    if l1>0 and l2>0 and norm(target)>0:
                        delta=vsub(target,origin); d=min(max(norm(delta),abs(l1-l2)+1e-5),l1+l2-1e-5)
                        along=unit(delta); pole=(0,0,1 if 'thigh' in p['name'].lower() else -1)
                        bend=unit(vsub(pole,vmul(along,dot(pole,along))))
                        if norm(bend)<.1:bend=(1,0,0)
                        a=(l1*l1-l2*l2+d*d)/(2*d);h=math.sqrt(max(0,l1*l1-a*a))
                        knee=vadd(origin,vadd(vmul(along,a),vmul(bend,h)))
                        end=vadd(origin,vmul(along,d))
                        upper=orient_z(vsub(knee,origin),(1,0,0))
                        lower=orient_z(vsub(end,knee),(1,0,0))
                        world[i]=(upper,origin)
                        overrides[mid]=(lower,knee)
                        overrides[tip]=(rotation(get(state,tip,'rotation')),end)
        for j in children[i]:visit(j)
    for root in roots:visit(root)
    return world


def local_transforms(actor,world):
    out={}
    for p in actor['parts']:
        r,o=world[p['id']]; par=p['parent']
        if par is not None:
            pr,po=world[par]; inv=transpose(pr); r,o=mm(inv,r),mv(inv,vsub(o,po))
        out[p['id']]=(r,o)
    return out


def bounds(actor,state,world):
    lo=[float('inf')]*3;hi=[-float('inf')]*3
    for p in actor['parts']:
        i=p['id']; axes=get(state,i,'halfaxes')
        if max(map(abs,axes))==0 or p['name'].lower()=='shadow':continue
        r,o=world[i];c=vadd(o,mv(r,get(state,i,'centre')))
        for j in range(3):
            extent=math.sqrt(sum((r[j][k]*axes[k])**2 for k in range(3)))
            lo[j]=min(lo[j],c[j]-extent);hi[j]=max(hi[j],c[j]+extent)
    if not math.isfinite(lo[0]):return (-1,-1,-1),(1,1,1)
    return lo,hi


def write_xml(path,root):
    ET.indent(root,space='  ')
    ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)


def options(actor,overrides):
    available=sorted({f for p in actor['parts'] if (f:=feature(p['name']))})
    return {f:overrides.get(f,f not in ('fingers','effects')) for f in available}


def reconstruct_shoulders(actor):
    """Add explicit preview repairs where a biped has no source shoulder caps."""
    names={p['name'].lower():p for p in actor['parts']};chest=names.get('chest')
    if chest is None:return actor
    state=fields(actor);axes=get(state,chest['id'],'halfaxes');center=get(state,chest['id'],'centre')
    if min(map(abs,axes))<1:return actor
    additions=[]
    for side in ('left','right'):
        arm=names.get(side+' upper arm')
        if not arm or side+' shoulder' in names or arm['parent']!=chest['id']:continue
        delta=tuple(get(state,arm['id'],'offset'));radial=math.sqrt(sum((v/a)**2 for v,a in zip(delta,axes)))
        if radial<=1:continue
        arm_axes=get(state,arm['id'],'halfaxes');arm_center=get(state,arm['id'],'centre')
        radius=1.15*max(abs(arm_axes[0]),abs(arm_axes[1]),abs(arm_axes[2])-abs(arm_center[2]))
        if radius<=0:continue
        direction=unit(delta);root=vadd(center,delta)
        inside=vadd(center,vsub(vmul(delta,1/radial),vmul(direction,radius*.5)))
        middle=vmul(vadd(root,inside),.5);r=orient_z(vsub(root,inside),(0,0,1))
        values={'offset':vsub(middle,center),'centre':(0,0,0),'rotation':vmul(euler(r),65536/360),
                'halfaxes':(radius,radius,norm(vsub(root,inside))*.5+radius),
                'colour':get(state,arm['id'],'colour'),'flags_command':(0,0,0)}
        additions.append(dict(id=900000+len(additions),name=side.title()+' shoulder',parent=chest['id'],
                              reconstructed='Preview shoulder connection; absent from decoded source',
                              fields={key:dict(values=list(value)) for key,value in values.items()}))
    if not additions:return actor
    result=copy.deepcopy(actor);result['parts'].extend(additions)
    return result


def source_prefab(actor,scale,overrides):
    actor=reconstruct_shoulders(actor)
    state=fields(actor);world=pose(actor,state);local=local_transforms(actor,world)
    lo,hi=bounds(actor,state,world)
    root=ET.Element('prefab')
    root.append(ET.Comment('Generated experimental source pose; see source.json and conversion.json. Z up, front -Y; centimetres.'))
    opts=options(actor,overrides)
    for name,enabled in opts.items():ET.SubElement(root,'option',name=name,enabled=str(int(enabled)))
    basis=ET.SubElement(root,'group',name='source_basis',rot='-90 0 180',scale=fmt((scale,)*3),pos=fmt(((lo[0]+hi[0])*.5*scale,(lo[2]+hi[2])*.5*scale,hi[1]*scale)))
    nodes={}
    for p in actor['parts']:
        r,o=local[p['id']]
        parent=basis if p['parent'] is None else nodes[p['parent']]
        node=ET.SubElement(parent,'group',name=joint_name(p),pos=fmt(o),rot=fmt(euler(r)))
        nodes[p['id']]=node
        axes=get(state,p['id'],'halfaxes')
        if max(map(abs,axes))==0 or p['name'].lower()=='shadow':continue
        colour=PALETTE[get(state,p['id'],'colour')[0]%len(PALETTE)]
        shape=ET.SubElement(node,'ellipsoid',radii=fmt(tuple(max(abs(v),.35) for v in axes)),
            pos=fmt(get(state,p['id'],'centre')),color=fmt(colour),rings='10',slices='16')
        f=feature(p['name'])
        if f:shape.set('if-feature',f)
        if 'hand' in p['name'].lower() and 'gloves' in opts:
            shape.set('if-feature','gloves');bare=copy.deepcopy(shape);bare.attrib.pop('if-feature')
            bare.set('unless-feature','gloves');bare.set('color',fmt(PALETTE[3]));node.append(bare)
    return root,state,local,(lo,hi)


def source_clip(actor,action,rest_state,rest_local,duration):
    clip=ET.Element('clip',name=f'action_{action["id"]}',length=fmt(duration),ease='linear',loop='0')
    state=copy.deepcopy(rest_state)
    byid={p['id']:p for p in actor['parts']}
    supported={1:'rotation',2:'offset',31:'position'}
    ignored={}; unknown_parts=set()
    end=max((k['phase'] for k in action['keys']),default=65535) or 65535
    for key in action['keys']:
        for e in key['events']:
            if e['opcode'] in supported and e['index'] in state:
                state[e['index']][supported[e['opcode']]]=e['values']
            elif e['opcode'] in supported:unknown_parts.add(e['index'])
            else:ignored[e['name']]=ignored.get(e['name'],0)+1
        transforms=local_transforms(actor,pose(actor,state))
        node=ET.SubElement(clip,'key',t=fmt(key['phase']/end*duration))
        for i,(r,o) in transforms.items():
            rr,ro=rest_local[i];delta=mm(transpose(rr),r);offset=mv(transpose(rr),vsub(o,ro))
            ET.SubElement(node,'joint',target=joint_name(byid[i]),rot=fmt(euler(delta)),pos=fmt(offset))
    return clip,dict(ignored_events=ignored,unmapped_part_ids=sorted(unknown_parts),
                     timing='unsigned source phase normalized to requested duration; not recovered game FPS')


def study(source,height=180,clip=None,overrides=None):
    h=max(10,height)
    root=ET.Element('scene',up='z',ambient='.28 .28 .28',background='.13 .15 .17')
    for name,pos in [('Standing',(-h*1.5,-h*2.7,h*.95)),('Front',(0,-h*3,h*.6)),('Side',(-h*3,0,h*.6))]:
        ET.SubElement(root,'camera',name=name,pos=fmt(pos),look=fmt((0,0,h*.5)),fov='30')
    ET.SubElement(root,'light',pos=fmt((-h,-h*1.5,h*2)),color='1 .94 .85',intensity='1.6',radius=fmt(h*5),castShadows='1')
    ET.SubElement(root,'light',pos=fmt((h,-h,h)),color='.8 .87 1',intensity='.7',radius=fmt(h*5),castShadows='0')
    ET.SubElement(root,'box',pos='0 0 -2',size=fmt((h*3,h*3,4)),color='.28 .29 .27')
    if clip is not None:root.append(clip)
    instance=ET.SubElement(root,'prefab',source=source,name='Actor')
    if clip is not None:ET.SubElement(instance,'layer',clip=clip.get('name'))
    for k,v in (overrides or {}).items():ET.SubElement(instance,'option',name=k,enabled=str(int(v)))
    return root


def cat_prefab(actor,preset,scale,overrides):
    """Adapt source limb halfaxes to an editable CAT Base Human preset.

    Intentionally distinct from source.blk: neutral symmetric rig, authored
    attachments and original face volumes, no claim to preserve the original rest pose.
    """
    parts={p['name'].lower():p for p in actor['parts']}
    if not all(x in parts for x in ['body','chest','head','left thigh','left shin','left upper arm','left forearm','left hand']):return None
    root=ET.parse(preset).getroot()
    for parent in root.iter():
        for child in list(parent):
            if child.tag=='option' or child.get('name','') in ('left_glove_cuff','left_glove_hand'):parent.remove(child)
    root.insert(0,ET.Comment('CAT adaptation generated from source dimensions. Attachments and neutral pose follow the editable preset; face volumes come from the source.'))
    opts=options(actor,overrides)
    opts.setdefault('gloves',False);opts.setdefault('boots',True);opts.setdefault('hair',True)
    for name,enabled in opts.items():ET.SubElement(root,'option',name=name,enabled=str(int(enabled)))
    materials={m.get('id'):m for m in root.findall('material')}
    def recolour(material,part):
        if part in parts:materials[material].set('color',fmt(PALETTE[parts[part]['fields']['colour']['values'][0]%16]))
    for m,p in [('skin','head'),('shirt','chest'),('trousers','body'),('shoes','left foot'),('hair','hair')]:recolour(m,p)
    if 'glove' not in materials:ET.SubElement(root,'material',id='glove',color=fmt(PALETTE[1]),shininess='5')
    mapped={}
    for target,part,axes in [('pelvis','body',(0,2,1)),('ribcage','chest',(0,2,1)),('head','head',(0,2,1)),
        ('left_thigh','left thigh',(0,1,2)),('left_calf','left shin',(0,1,2)),
        ('left_upperarm','left upper arm',(0,1,2)),('left_forearm','left forearm',(0,1,2))]:
        node=root.find(f'.//*[@name="{target}"]');p=parts[part];a=[abs(v)*scale for v in p['fields']['halfaxes']['values']]
        node.set('radius',fmt((max(a[axes[0]],.5),max(a[axes[1]],.5))))
        if target.startswith('left_'):node.set('length',fmt(max(2,2*a[axes[2]]-2*float(node.get('overlap','2')))))
        mapped[target]=p['id']
    # Keep pelvis/ribcage lengths from the CAT preset so added spine remains flexible.
    forearm=root.find('.//*[@name="left_forearm"]');palm=root.find('.//*[@name="left_palm"]')
    for p in actor['parts']:
        if 'left glove' not in p['name'].lower():continue
        a=p['fields']['halfaxes']['values']
        ET.SubElement(forearm,'ellipsoid',{'name':'left_glove_'+str(p['id']),'on':'0 0','at':'.76','sink':forearm.get('radius').split()[1],
            'radii':fmt([max(abs(v)*scale,.1) for v in a]),'material':'glove','if-feature':'gloves'})
    # Two hand appearances share one palm joint; the feature never removes IK controls.
    palm.set('volume','0')
    for condition,material in [('if-feature','glove'),('unless-feature','skin')]:
        ET.SubElement(palm,'ellipsoid',{'on':'0 0','at':'.5','sink':palm.get('radius').split()[1],
            'radii':'3.8 4.8 7','material':material,condition:'gloves'})
    hair=root.find('.//*[@name="hair"]');hair.set('volume','0')
    ET.SubElement(hair,'ellipsoid',{'on':'0 0','at':'.5','sink':'9.5','radii':'8.5 9.5 7','material':'hair','if-feature':'hair'})
    calf=root.find('.//*[@name="left_calf"]')
    for p in actor['parts']:
        if p['name'].lower()=='left boot':
            ET.SubElement(calf,'ellipsoid',{'on':'0 0','at':'.6','sink':calf.get('radius').split()[1],
                'radii':fmt([max(abs(v)*scale,.1) for v in p['fields']['halfaxes']['values']]),'material':'shoes','if-feature':'boots'})
    # Keep the source face/hair volumes attached to the CAT head control.
    head=root.find('.//*[@name="head"]')
    source,state,_,_=source_prefab(actor,scale,overrides)
    head_part=parts['head'];source_head=source.find(f'.//*[@name="{joint_name(head_part)}"]')
    if source_head is not None:
        for child in list(head):head.remove(child)
        head.set('volume','0')
        basis=(( -1,0,0),(0,0,-1),(0,-1,0))
        head_rotation=mm(basis,pose(actor,state)[head_part['id']][0])
        center=mv(head_rotation,get(state,head_part['id'],'centre'))
        source_head.set('rot',fmt(euler(head_rotation)))
        source_head.set('pos',fmt(vsub((0,0,float(head.get('length'))*.5),vmul(center,scale))))
        source_head.set('scale',fmt((scale,)*3))
        for p in actor['parts']:
            if p['parent']==head_part['id'] and p['name'].lower()=='neck':
                neck=source_head.find(f'./*[@name="{joint_name(p)}"]')
                if neck is not None:source_head.remove(neck)
        head.append(copy.deepcopy(source_head))
    # Gloves do not enable fingers: the user's default remains a single mitten.
    root.set('fingers',str(int(overrides.get('fingers',False))))
    return root,mapped
