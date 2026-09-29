import contextlib
import math
from pathlib import Path
import struct
import tempfile
import unittest
import xml.etree.ElementTree as ET

from ecstatica_archive import Iso9660, DecodeError, prefix, event_stream, action_defs, read_disc
from ecstatica_decode import Reader, NAMES
from ecstatica_convert import rotation, euler, fmt, slug, source_prefab, source_clip, fields
from ecstatica_import import import_disc


def event(op=0,index=0,values=(0,0,0)):
    return struct.pack('>5h',op,index,*values)


def fant(version=30,main=False,actor=b'',action=b''):
    header=b'FANT'+struct.pack('>2h',version,0 if main else 1)+bytes(26)
    if main:
        for name in NAMES+(['textures'] if version==55 else []):
            header+=(b'Body\0' if name=='parts' else b'Test\0' if name=='actors' else b'')+b'\0'
    return header+action+event()+actor+event()+event()+b'\0\0'+event()


def record(name,block,size,flags=0):
    name=name if isinstance(name,bytes) else name.encode('ascii')
    n=33+len(name)+(not len(name)%2)
    data=bytearray(n); data[0]=n
    struct.pack_into('<I',data,2,block);struct.pack_into('>I',data,6,block)
    struct.pack_into('<I',data,10,size);struct.pack_into('>I',data,14,size)
    data[25]=flags;data[28:32]=b'\1\0\0\1';data[32]=len(name);data[33:33+len(name)]=name
    return data


def make_iso(path):
    main=fant(main=True)
    actor=fant(actor=event(8,0,(0,1,2))+event(10,0,(0,1,2))+event(4,0,(10,20,30))+event(3,0,(3,0,0)))
    offsets=[0xffffffff]*3525;offsets[1000]=0
    table=struct.pack('>3525I',*offsets)
    data=bytearray(36*2048)
    pvd=bytearray(2048);pvd[0:7]=b'\1CD001\1';struct.pack_into('<H',pvd,128,2048);pvd[156:190]=record(b'\0',20,2048,2)
    data[16*2048:17*2048]=pvd
    def directory(block,entries):
        content=record(b'\0',block,2048,2)+record(b'\1',20,2048,2)+b''.join(entries)
        data[block*2048:block*2048+len(content)]=content
    directory(20,[record('CODE',21,2048,2),record('FILES',22,2048,2),record('OFFSETS;1',23,len(table))])
    directory(21,[record('ECSTATIC.FAN;1',31,len(main))]);directory(22,[record('ECSTATIC;1',32,len(actor))])
    for block,payload in [(23,table),(31,main),(32,actor)]:data[block*2048:block*2048+len(payload)]=payload
    path.write_bytes(data)


class ImportTests(unittest.TestCase):
    def test_iso_direct_read_and_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'game with spaces.iso';make_iso(path)
            with contextlib.closing(Iso9660(path)) as iso:
                self.assertEqual(iso.read('code/ecstatic.fan')[:4],b'FANT')
                self.assertEqual(len(iso.members),3)
            d=read_disc(path)
            self.assertEqual(d['game'],'ecstatica1');self.assertEqual(len(d['actors']),1)
            self.assertEqual(d['actors'][0]['parts'][0]['fields']['halfaxes']['values'],[10,20,30])

    def test_iso_rejects_bad_extents(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.iso';make_iso(path);data=bytearray(path.read_bytes())
            struct.pack_into('<I',data,16*2048+156+2,9999)
            path.write_bytes(data)
            with self.assertRaisesRegex(DecodeError,'endian'):Iso9660(path)

    def test_truncated_iso(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.iso';path.write_bytes(b'FANT')
            with self.assertRaises(DecodeError):Iso9660(path)

    def test_v55_texture_names_and_unknown_events(self):
        d=prefix(fant(55,True,actor=event(70,3,(4,5,6))))
        self.assertEqual(d['names']['textures'],[])
        self.assertEqual(d['actor_events'][0]['name'],'UNKNOWN_70')
        self.assertEqual(d['actor_events'][0]['values'],[4,5,6])
        self.assertGreater(d['unparsed_tail_bytes'],0)

    def test_bad_version_opcode_and_truncated_stream(self):
        for data in [fant(54),fant(actor=event(99)),fant()[:39]]:
            with self.assertRaises(DecodeError):prefix(data)

    def test_unsigned_phase_and_order(self):
        names={'actions':['walk']}
        stream=event(11)+event(12,0,(-32768,0,0))+event(12,0,(-1,0,0))+event()
        a=action_defs(event_stream(Reader(stream),30),names)[0]
        self.assertEqual([k['phase'] for k in a['keys']],[32768,65535])
        stream=event(11)+event(12,0,(10,0,0))+event(12,0,(9,0,0))+event()
        with self.assertRaisesRegex(DecodeError,'non-monotonic'):action_defs(event_stream(Reader(stream),30),names)

    def test_rotation_matrix_roundtrip(self):
        for angles in [(1000,2000,3000),(-16384,0,8192),(0,16384,500)]:
            m=rotation(angles);back=rotation([x*65536/360 for x in euler(m)])
            for a,b in zip(sum(m,()),sum(back,())):self.assertAlmostEqual(a,b,places=6)

    def test_portable_output_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'disc.iso';make_iso(path);out=Path(tmp)/'out'
            result=import_disc(path,out,Path('unused.blk'))
            self.assertEqual(len(result['actors']),1)
            dest=out/'ecstatica1'/result['actors'][0]['path']
            scene=ET.parse(dest/'scenes/preview.blks').getroot()
            self.assertTrue((dest/'prefabs'/(scene.find('prefab').get('source')+'.blk')).is_file())
            ET.parse(dest/'prefabs/source.blk')
            with self.assertRaisesRegex(DecodeError,'already contains'):import_disc(path,out,Path('unused.blk'))

    def test_unclassified_flag_never_hides_anatomy(self):
        actor={'parts':[{'id':i,'name':name,'parent':None,'fields':{
            'halfaxes':{'values':[20,31,72]},'flags_command':{'values':[flags,-1,0]}}}
            for i,(name,flags) in enumerate([('Left thigh',9376),('Left shin',1024),('Right calf',5120),('Body',1024)])]}
        prefab,_,_,_=source_prefab(actor,.207,{})
        self.assertIsNone(prefab.find("option[@name='effects']"))
        self.assertEqual(len(prefab.findall('.//ellipsoid')),4)
        self.assertTrue(all('if-feature' not in n.attrib for n in prefab.iter('ellipsoid')))

    def test_options_and_clip_reports(self):
        actor={'parts':[{'id':0,'name':'Left Glove','parent':None,'fields':{
            'halfaxes':{'values':[2,3,4]},'offset':{'values':[0,0,0]}}}]}
        prefab,state,local,box=source_prefab(actor,.2,{'gloves':False})
        self.assertEqual(prefab.find('option').get('enabled'),'0')
        self.assertEqual(prefab.find('.//ellipsoid').get('if-feature'),'gloves')
        action={'id':5,'keys':[{'phase':65535,'events':[
            {'opcode':1,'name':'ROTATE','index':0,'values':[0,0,16384]},
            {'opcode':3,'name':'COLOUR','index':0,'values':[1,0,0]}]}]}
        clip,report=source_clip(actor,action,state,local,2)
        self.assertEqual(clip.find('key').get('t'),'2')
        self.assertEqual(report['ignored_events'],{'COLOUR':1})
        self.assertAlmostEqual(float(clip.find('.//joint').get('rot').split()[2]),90)

    def test_slug_cannot_escape_output(self):
        self.assertEqual(slug('../../Bad / Name'),'bad-name')
        self.assertEqual(slug(''),'unnamed')


if __name__=='__main__':unittest.main()
