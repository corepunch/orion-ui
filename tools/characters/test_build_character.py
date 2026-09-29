import copy
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib

from build_character import appearance,build,validate,reference,DEFAULTS,BASES,RANGES,BONES,REPO


def native_joints(folder,scene='study',time=0):
    r=subprocess.run([os.environ['SCENER'],'--list-joints',str(folder/'scenes'/f'{scene}.blks'),'--time',str(time)],capture_output=True,text=True,check=True)
    if 'out of reach' in r.stderr:raise AssertionError(r.stderr)
    return {a[1]:tuple(map(float,a[2:])) for line in r.stdout.splitlines() if (a:=line.split()) and a[0]=='Character'}


def png_pixels(path):
    data=path.read_bytes();pos=8;packed=b'';width=height=kind=None
    while pos<len(data):
        size=struct.unpack_from('>I',data,pos)[0];tag=data[pos+4:pos+8];chunk=data[pos+8:pos+8+size];pos+=size+12
        if tag==b'IHDR':width,height,depth,kind=struct.unpack('>IIBB',chunk[:10]);assert depth==8 and kind in (2,6)
        if tag==b'IDAT':packed+=chunk
    bpp=4 if kind==6 else 3;stride=width*bpp;raw=zlib.decompress(packed);previous=bytearray(stride);pixels=bytearray()
    for y in range(height):
        code=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a=row[x-bpp] if x>=bpp else 0;b=previous[x];c=previous[x-bpp] if x>=bpp else 0
            if code==1:add=a
            elif code==2:add=b
            elif code==3:add=(a+b)//2
            elif code==4:
                p=a+b-c;da,db,dc=abs(p-a),abs(p-b),abs(p-c);add=a if da<=db and da<=dc else b if db<=dc else c
            else:assert code==0;add=0
            row[x]=(row[x]+add)&255
        pixels.extend(row);previous=row
    return pixels


class SourceCharacterTests(unittest.TestCase):
    def test_rejects_obsolete_and_invalid_controls(self):
        for spec in ({'age':40},{'body_fat':.4},{'base':'missing'},{'height_heads':float('nan')},{'shoulders':True},{'youth':2},{'hair':1}):
            with self.subTest(spec=spec),self.assertRaises(ValueError):validate(spec)

    def test_source_faces_and_part_identity_preserved(self):
        for base in BASES:
            root,_=appearance({'base':base})
            for p in reference(base)['parts']:
                name=f"p{p['id']}_"
                self.assertTrue(any(n.get('name','').startswith(name) for n in root.iter('group')))
            self.assertIsNone(root.find(".//*[@name='face']"))
            self.assertIsNone(root.find(".//*[@name='belly_surface']"))
            self.assertEqual(root.find("option[@name='fingers']").get('enabled'),'0')

    def test_width_deforms_chest_and_moves_arm_attachments_together(self):
        from ecstatica_convert import norm
        for base in BASES:
            narrow,_=appearance({'base':base,'shoulders':.7});wide,_=appearance({'base':base,'shoulders':1.3})
            def shoulder_x(root):
                n=root.find(".//*[@name='left_arm']");a,e=map(math.radians,map(float,n.get('from').split()));distance=1-float(n.get('sink'))
                return math.sin(a)*math.cos(e)*distance
            self.assertAlmostEqual(shoulder_x(wide)/shoulder_x(narrow),1.3/.7,places=5)
            for label in ('p1_chest','p40_back'):
                n=narrow.find(f".//*[@name='{label}']");w=wide.find(f".//*[@name='{label}']")
                if n is not None:
                    def width(node):return float(node.find('./group/group/group').get('scale').split()[0])
                    self.assertAlmostEqual(width(w)/width(n),1.3/.7,places=5)

    def test_fullness_changes_every_source_body_volume(self):
        def volume(node,scale=1):
            scale*=math.prod(map(float,node.get('scale','1 1 1').split()))
            own=math.prod(map(float,node.get('radii').split()))*scale if node.tag=='ellipsoid' else 0
            return own+sum(volume(child,scale) for child in node)
        for base in BASES:
            lean,_=appearance({'base':base,'fullness':-1});full,_=appearance({'base':base,'fullness':1})
            for part in reference(base)['parts']:
                from ecstatica_convert import joint_name
                name=joint_name(part);a=lean.find(f".//group[@name='{name}']");b=full.find(f".//group[@name='{name}']")
                if a is None or not volume(a):continue
                with self.subTest(base=base,part=part['name']):self.assertGreater(volume(b),volume(a)*1.05)

    def test_height_preserves_head_hands_and_feet(self):
        for base in BASES:
            short,_=appearance({'base':base,'height_heads':3});tall,_=appearance({'base':base,'height_heads':9})
            from ecstatica_cat import calibrated_cat
            from ecstatica_convert import joint_name
            _,cal=calibrated_cat(reference(base))
            for part in reference(base)['parts']:
                owner=cal['bindings'][part['id']]['owner']
                if owner!='head' and not owner.endswith(('_palm','_foot')):continue
                name=joint_name(part)
                a=short.find(f".//group[@name='{name}']");b=tall.find(f".//group[@name='{name}']")
                with self.subTest(base=base,part=part['name']):self.assertEqual(ET.tostring(a),ET.tostring(b))

    def test_frame_changes_structure_independently_of_muscle(self):
        for base in BASES:
            narrow,_=appearance({'base':base,'frame':.7,'muscle':-1})
            broad,_=appearance({'base':base,'frame':1.6,'muscle':-1})
            def attachment(node):
                az,el=map(math.radians,map(float,node.get('from').split()));length=1-float(node.get('sink'))
                return (math.sin(az)*math.cos(el)*length,math.sin(el)*length)
            for role in ('left_arm','right_arm','left_leg','right_leg'):
                a=attachment(narrow.find(f".//*[@name='{role}']"));b=attachment(broad.find(f".//*[@name='{role}']"))
                self.assertAlmostEqual(b[0]/a[0],1.6/.7,places=4)
                self.assertAlmostEqual(b[1],a[1],places=4)
            for part in reference(base)['parts']:
                from ecstatica_convert import joint_name
                name=joint_name(part);a=narrow.find(f".//group[@name='{name}']");b=broad.find(f".//group[@name='{name}']")
                if part['name'].lower() in ('head','cheeks'):
                    self.assertEqual(ET.tostring(a),ET.tostring(b))
                if part['name'].lower() in ('body','chest','left thigh','left foot','left hand','neck'):
                    sa=tuple(map(float,a.find('group').get('scale').split()));sb=tuple(map(float,b.find('group').get('scale').split()))
                    self.assertAlmostEqual(sb[0]/sa[0],1.6/.7,places=5)
                    self.assertAlmostEqual(sb[1]/sa[1],1.6/.7,places=5)
                    self.assertEqual(sb[2],sa[2])

    def test_low_muscle_preserves_shoulder_coverage_and_chest_surface(self):
        from ecstatica_convert import fields,get,SCALE,mv,vadd,vsub,vmul,transpose,joint_name
        from ecstatica_cat import calibrated_cat
        for base in BASES:
            actor=reference(base);_,cal=calibrated_cat(actor);state=fields(actor)
            parts={p['name'].lower():p for p in actor['parts']};root,_=appearance({'base':base,'muscle':-1})
            def geometry(part):
                b=cal['bindings'][part['id']];r=b['rotation']
                center=vadd(cal['controls'][b['owner']]['position'],vadd(b['offset'],vmul(mv(r,get(state,part['id'],'centre')),SCALE)))
                shape=root.find(f".//group[@name='{joint_name(part)}']").find('.//ellipsoid')
                return center,r,tuple(map(float,shape.get('radii').split()))
            def radial_distance(point,shape):
                c,r,axes=shape;q=mv(transpose(r),vsub(point,c))
                return sum((v/a)**2 for v,a in zip(q,axes))
            for side in ('left','right'):
                cap=parts.get(side+' shoulder');pec=parts.get(side+' pectorals')
                if cap:
                    c,r,axes=geometry(parts[side+' upper arm']);tip=vadd(c,mv(r,(0,0,-axes[2])))
                    with self.subTest(base=base,side=side,surface='shoulder'):
                        self.assertLess(radial_distance(tip,geometry(cap)),1)
                if pec:
                    c,r,axes=geometry(pec)
                    extent=math.sqrt(sum((r[1][j]*axes[j])**2 for j in range(3)))
                    front=tuple(c[i]-sum(r[i][j]*r[1][j]*axes[j]**2 for j in range(3))/extent for i in range(3))
                    with self.subTest(base=base,side=side,surface='chest'):
                        self.assertGreater(radial_distance(front,geometry(parts['chest'])),1)

    def test_missing_source_shoulders_are_repaired_without_changing_raw_actor(self):
        from ecstatica_convert import reconstruct_shoulders,joint_name,source_prefab,SCALE,source_clip,fields
        from ecstatica_cat import calibrated_cat,calibrated_clip
        raw=reference('villager-slim');before=copy.deepcopy(raw);fixed=reconstruct_shoulders(raw)
        repairs=[p for p in fixed['parts'] if p.get('reconstructed')]
        self.assertEqual([p['name'] for p in repairs],['Left shoulder','Right shoulder'])
        self.assertEqual(raw,before)
        self.assertEqual(reconstruct_shoulders(fixed),fixed)
        source,state,local,_=source_prefab(raw,SCALE,{})
        controlled,cal=calibrated_cat(raw);generated,_=appearance({'base':'villager-slim'})
        for part in repairs:
            for tree in (source,controlled,generated):
                self.assertIsNotNone(tree.find(f".//*[@name='{joint_name(part)}']//ellipsoid"))
            self.assertEqual(cal['bindings'][part['id']]['owner'],'ribcage')
        action={'id':1,'keys':[{'phase':65535,'events':[{'opcode':1,'index':1,'values':[4096,0,0]}]}]}
        self.assertIsNotNone(calibrated_clip(raw,action,cal))
        self.assertIsNotNone(source_clip(raw,action,state,local,1))

    def test_fixed_cameras_and_non_destructive_export(self):
        with tempfile.TemporaryDirectory() as temp:
            a=build({'height_heads':3},Path(temp)/'a');b=build({'height_heads':9},Path(temp)/'b')
            cameras=lambda folder:[n.attrib for n in ET.parse(folder/'scenes/study.blks').getroot().findall('camera')]
            self.assertEqual(cameras(a),cameras(b))
            with self.assertRaises(ValueError):build({},a)

    @unittest.skipUnless(os.environ.get('SCENER'),'set SCENER for native rig evaluation')
    def test_all_slider_endpoints_on_all_sources_are_finite(self):
        with tempfile.TemporaryDirectory() as temp:
            for base in BASES:
                for key,limits in RANGES.items():
                    for end,value in enumerate(limits):
                        with self.subTest(base=base,control=key,value=value):
                            folder=build({'base':base,key:value},Path(temp)/f'{base}-{key}-{end}')
                            joints=native_joints(folder)
                            self.assertTrue(all(math.isfinite(v) for point in joints.values() for v in point))
                            self.assertGreater(joints['head'][2],joints['pelvis'][2])
                            self.assertIn('left_calf',joints);self.assertIn('right_calf',joints)

    @unittest.skipUnless(os.environ.get('SCENER'),'set SCENER for native motion evaluation')
    def test_source_controls_still_pose(self):
        with tempfile.TemporaryDirectory() as temp:
            for base in BASES:
                folder=build({'base':base},Path(temp)/base);rest=native_joints(folder)
                for scene in ('bend','present','walk'):
                    with self.subTest(base=base,scene=scene):
                        moving=native_joints(folder,scene,.4)
                        key={'bend':'head','present':'left_palm','walk':'left_foot'}[scene]
                        self.assertNotEqual(rest[key],moving[key])

    @unittest.skipUnless(os.environ.get('SCENER_RENDER'),'set SCENER_RENDER for GPU slider visibility tests')
    def test_every_slider_changes_visible_pixels_in_fixed_frame(self):
        with tempfile.TemporaryDirectory() as temp:
            for key,limits in RANGES.items():
                samples=[]
                for end,value in enumerate(limits):
                    folder=build({key:value},Path(temp)/f'{key}-{end}');camera='Side' if key=='posture' else 'Front'
                    subprocess.run([os.environ['SCENER_RENDER'],'--render',str(folder/'scenes/study.blks'),'--camera',camera,'--size','400x400','--format','png','--output-dir',str(folder/'render')],capture_output=True,text=True,check=True)
                    samples.append(png_pixels(folder/'render'/f'{camera}.png'))
                changed=sum(abs(a-b)>8 for a,b in zip(*samples))/len(samples[0])
                print(f'[slider-visibility] {key}: {changed:.1%} channels visibly changed')
                self.assertGreater(changed,.003,key)


if __name__=='__main__':unittest.main()
