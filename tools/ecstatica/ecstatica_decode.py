#!/usr/bin/env python3
"""Bounded decoder for the supplied Ecstatica 1 v30 disc. See FORMAT.md.

Format research: Fabian Hachenberg's pyecstaticalib, commit
17be427f02166a4077eae3e129e93cd6d10cea0f. This is an independent parser;
no third-party package or game executable is loaded or executed.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

NAMES = 'parts actors actions scenes points triangles codes repertoires sounds map_areas'.split()
OPCODES = '''NO_EVENT ROTATE OFFSET COLOUR VECTOR1 VECTOR2 VECTOR3 ADD_PART
ADD_THING TYPE ADD_PART_TO_THING PSEUDO_ACTION PSEUDO_KEY DISP_PNT FLAGS
MOVE_ACT RAND_ACT RAND_INFO ROTATE_THING MOVE_THING START_POSITION THING_FLAGS
SCRIPT_MOVE SCRIPT_TURN SPAWN_ACTION PSEUDO_SCENE PSEUDO_SCRIPT NEXT_SCENE
ANCHOR_PART LOOSEN_JOINT UNLOOSEN_JOINT POSITION 2_PART_LIMB FIX_PART UNFIX_PART
UNMAKE_LIMB REORIENT_THING PSEUDO_ADJUNCT PSEUDO_ADJUNCT_2 ABSOLUTE_POS
ABSOLUTE_ROT ADD_POINT OFFSET_POINT ADD_TRIANGLE COLOUR_TRIANGLE TRIANGLE_FLAGS
INTERACT PSEUDO_ACTION_2 POINT_TO_POINT HELD_OFFSET HELD_ROTATE BACKGROUND
PSEUDO_SCENE_2 PSEUDO_REP REP_ENTRY ACTOR_REP DEF_ROTATE DEF_OFFSET DEF_VECTOR1
DEF_VECTOR2 DEF_COLOUR DEF_FLAGS DEF_POSITION CUT_PART HELD_OFF_LEFT
HELD_ROT_LEFT THING_CODE'''.split()
FIELDS = {1: 'rotation', 2: 'offset', 3: 'colour', 4: 'halfaxes',
          5: 'centre', 6: 'vector3', 9: 'type', 13: 'display_point',
          14: 'flags_command', 31: 'position'}
RANGES = [(0, 1000, 'scenes'), (1000, 1500, 'actors'), (1500, 2500, 'actions'),
          (2500, 2650, 'repertoires'), (2650, 3150, 'sounds'), (3150, 3525, 'other')]


class DecodeError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if count < 0 or count > len(self.data) - self.pos:
            raise DecodeError(f'at 0x{self.pos:x}: need {count}, have {len(self.data)-self.pos}')
        start = self.pos
        self.pos += count
        return self.data[start:self.pos]

    def unpack(self, fmt):
        values = struct.unpack(fmt, self.take(struct.calcsize(fmt)))
        return values[0] if len(values) == 1 else list(values)

    def string(self):
        end = self.data.find(b'\0', self.pos)
        if end < 0:
            raise DecodeError(f'unterminated string at 0x{self.pos:x}')
        return self.take(end - self.pos + 1)[:-1].decode('latin1')

    def count(self, fmt, minimum_size):
        value = self.unpack(fmt)
        if value < 0 or value > (len(self.data) - self.pos) // minimum_size:
            raise DecodeError(f'impossible count {value} at 0x{self.pos:x}')
        return value


def events(reader):
    result = []
    while True:
        start = reader.pos
        opcode, index, *values = reader.unpack('>5h')
        if not 0 <= opcode < len(OPCODES):
            raise DecodeError(f'unknown opcode {opcode} at 0x{start:x}')
        event = dict(offset=start, opcode=opcode, name=OPCODES[opcode], index=index, values=values)
        if opcode == 27 and index:
            event['attachment'] = reader.string()
        result.append(event)
        if opcode == 0:
            return result


def fant(data):
    r = Reader(data)
    if r.take(4) != b'FANT':
        raise DecodeError('missing FANT magic')
    version, skip = r.unpack('>2h')
    if version != 30 or skip not in (0, 1):
        raise DecodeError(f'unsupported version/skip {version}/{skip}')
    result = dict(version=version, skip_names=skip, reserved_hex=r.take(26).hex())
    if not skip:
        result['names'] = {}
        for name in NAMES:
            values = []
            while True:
                value = r.string()
                if not value:
                    break
                values.append(value)
            result['names'][name] = values
    for name in ('action_events', 'actor_events', 'scene_events'):
        result[name] = events(r)
    result['codes'] = []
    for _ in range(r.count('>h', 6)):
        code = dict(offset=r.pos, index=r.unpack('>h'), tokens=[], lines=[])
        while True:
            offset = r.pos
            token = r.unpack('>H')
            if not token:
                break
            item = dict(offset=offset, value=token)
            if token & 0xf000 == 0xe000:
                item['string_hex'] = r.take(((token & 0xfff) + 1) & ~1).hex()
            code['tokens'].append(item)
        for _ in range(r.count('>h', 1)):
            code['lines'].append(r.string())
        result['codes'].append(code)
    result['repertoire_events'] = events(r)
    result['sounds'] = []
    while r.unpack('B'):
        start = r.pos
        index, flags, unknown1, length, unknown2 = r.unpack('<hHhih')
        payload = r.take(length)
        result['sounds'].append(dict(offset=start, index=index, flags=flags,
                                     unknown1=unknown1, unknown2=unknown2,
                                     length=length, payload_hex=payload.hex()))
    result['sector_present'] = r.unpack('>h')
    if result['sector_present']:
        result['sector_map'] = r.unpack('<16384h')
        result['sectors'] = []
        for _ in range(r.count('>i', 10)):
            start = r.pos
            priority, section, camera, ymax, unknown, pad = r.unpack('BbBBbB')
            packed = r.unpack('>H')
            padding = r.take(2).hex()
            result['sectors'].append(dict(offset=start, priority=priority, section=section,
                camera=camera, ymax=ymax, unknown=unknown, padding=[pad, padding],
                code=packed & 0x3fff, flags=packed & 0xc000))
        result['cameras'] = [r.unpack('<7h') for _ in range(r.count('<h', 14))]
        result['map_areas'] = []
        while r.unpack('B'):
            index, count = r.unpack('<2h')
            if not 0 <= count <= 10:
                raise DecodeError(f'unsupported map-area count {count}')
            result['map_areas'].append(dict(index=index, values=[r.unpack('<h') for _ in range(count)]))
    result['consumed'] = r.pos
    return result


def actors(event_list, names):
    result, actor = [], None
    for e in event_list:
        op, idx, v = e['opcode'], e['index'], e['values']
        if op == 8:
            actor = dict(slot=v[0], offset=e['offset'], parts={}, events=[])
            result.append(actor)
        if actor is None or op == 0:
            continue
        actor['events'].append(e)
        if op in (7, 10):
            part_id = v[0]
            if part_id in actor['parts']:
                raise DecodeError(f'duplicate part {part_id}')
            actor['parts'][part_id] = dict(id=part_id, name=lookup(names['parts'], part_id),
                parent=None if op == 10 else idx, offset_in_file=e['offset'], fields={})
        elif op in FIELDS and idx in actor['parts']:
            actor['parts'][idx]['fields'][FIELDS[op]] = dict(values=v, offset=e['offset'])
    for actor in result:
        for part in actor['parts'].values():
            seen, parent = {part['id']}, part['parent']
            while parent is not None:
                if parent in seen or parent not in actor['parts']:
                    raise DecodeError(f'invalid parent chain for part {part["id"]}')
                seen.add(parent)
                parent = actor['parts'][parent]['parent']
        actor['parts'] = list(actor['parts'].values())
    return result


def lookup(names, index):
    return names[index].rstrip() if 0 <= index < len(names) else f'#{index}'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pixels(data, depth=False):
    """Decode the disc's run/delta packed view plane; reject overruns."""
    r, values, previous = Reader(data), [], 0
    mask = 65535 if depth else 255
    while r.pos < len(data):
        control = r.unpack('B')
        count, mode = control >> 2, control & 3
        if not count:
            if r.pos != len(data):
                raise DecodeError('data after pixel terminator')
            break
        if len(values) + count > 64000:
            raise DecodeError('pixel run exceeds 320 x 200')
        if mode == 0:
            packed = r.take((count + 1) // 2)
            for i in range(count):
                delta = (packed[i // 2] >> (4 * (i % 2))) & 15
                previous = (previous + (delta if delta < 8 else delta - 16) * (4 if depth else 1)) & mask
                values.append(previous)
        elif depth and mode == 1:
            for _ in range(count):
                previous = (previous + 4 * r.unpack('b')) & mask
                values.append(previous)
        elif mode == 2:
            for _ in range(count):
                previous = (4 * r.unpack('<H')) & mask if depth else r.unpack('B')
                values.append(previous)
        else:
            previous = (4 * r.unpack('<H')) & mask if depth else r.unpack('B')
            values.extend([previous] * count)
    if len(values) != 64000:
        raise DecodeError(f'expected 64000 pixels, got {len(values)}')
    return values


def visual_data(root):
    """Optional disc graphics. Keeps full decoded payload in JSON, outside repo."""
    result = dict(views=[], graphics=[])
    for path in sorted((root / 'VIEWS').glob('*.RAW')):
        data = path.read_bytes()
        r = Reader(data)
        signature, a, b = r.unpack('<Hii')
        if signature != 0:
            raise DecodeError(f'unsupported view signature: {path}')
        colour, depth = pixels(r.take(a)), pixels(r.take(b), True)
        count = r.count('<h', 2)
        tail = r.unpack(f'>{count}h') if count else []
        if r.pos != len(data):
            raise DecodeError(f'trailing view bytes: {path}')
        result['views'].append(dict(file=path.name, sha256=digest(data), bytes=len(data),
            packed_colour_bytes=a, packed_depth_bytes=b, colour=colour, depth=depth,
            tail_words=tail))
    for path in sorted((root / 'GRAPHICS').glob('*.RAW')):
        data = path.read_bytes()
        r = Reader(data)
        header = r.take(32)
        width, height = struct.unpack_from('>HH', header, 8)
        palette = r.take(768)
        image = r.take(width * height)
        if r.pos != len(data):
            raise DecodeError(f'trailing graphic bytes: {path}')
        result['graphics'].append(dict(file=path.name, sha256=digest(data), bytes=len(data),
            header_hex=header.hex(), width=width, height=height,
            palette_hex=palette.hex(), pixels_hex=image.hex()))
    path = root / 'SHADEMAP.DAT'
    if path.exists():
        data = path.read_bytes()
        if len(data) != 128 * 128 * 3:
            raise DecodeError('invalid shade map length')
        samples = list(struct.iter_unpack('>Bh', data))
        mismatches = 0
        for i, (_, depth) in enumerate(samples):
            x, y = i % 128, i // 128
            q = 1 - ((x - 63.5) / 63.5) ** 2 - ((y - 63.5) / 63.5) ** 2
            expected = int(-32768 * math.sqrt(q)) if q >= 0 else 32767
            mismatches += depth != expected
        result['shade_map'] = dict(sha256=digest(data), samples=samples,
                                  sphere_depth_mismatches=mismatches)
    return result


def category(index):
    for start, end, name in RANGES:
        if start <= index < end:
            return name, index - start
    raise DecodeError(f'index out of range: {index}')


def read_store(root, table_name, file_name, names):
    table, data = (root / table_name).read_bytes(), (root / file_name).read_bytes()
    if len(table) != 3525 * 4:
        raise DecodeError('offset table must contain 3525 entries')
    offsets = struct.unpack('>3525I', table)
    ordered = sorted(set(o for o in offsets if o != 0xffffffff))
    if not ordered or ordered[-1] >= len(data):
        raise DecodeError('offset outside container')
    ends = dict(zip(ordered, ordered[1:] + [len(data)]))
    result = dict(table=table_name, file=file_name, bytes=len(data), sha256=digest(data),
                  table_sha256=digest(table), records=[], missing=[])
    for index, start in enumerate(offsets):
        if start == 0xffffffff:
            result['missing'].append(index)
            continue
        payload = data[start:ends[start]]
        kind, local_id = category(index)
        item = dict(id=index, category=kind, local_id=local_id,
                    name=lookup(names.get(kind, []), local_id), offset=start,
                    size=len(payload), sha256=digest(payload))
        if payload.startswith(b'FANT'):
            item['fant'] = fant(payload)
            if item['fant']['consumed'] != len(payload):
                raise DecodeError(f'{table_name} record {index}: trailing {len(payload)-item["fant"]["consumed"]} bytes')
            item['actors'] = actors(item['fant']['actor_events'], names)
        else:
            r = Reader(payload)
            length = r.unpack('>I')
            if length != len(payload) - 4:
                raise DecodeError(f'invalid opaque record size: {index}')
            item['opaque'] = dict(length=length, payload_hex=r.take(length).hex())
        result['records'].append(item)
    return result


def extract(root):
    data = (root / 'CODE/ECSTATIC.FAN').read_bytes()
    main = fant(data)
    if main['consumed'] != len(data):
        raise DecodeError(f'main FANT: trailing {len(data)-main["consumed"]} bytes')
    names = main['names']
    return dict(main=dict(bytes=len(data), sha256=digest(data), fant=main,
                         actors=actors(main['actor_events'], names)),
                stores=[read_store(root, 'OFFSETS', 'FILES/ECSTATIC', names),
                        read_store(root, 'OFF2', 'FILES/ECST2', names)],
                visuals=visual_data(root))


def summary(decoded):
    result = dict(main_bytes=decoded['main']['bytes'],
                  names={k: len(v) for k, v in decoded['main']['fant']['names'].items()}, stores=[])
    for store in decoded['stores']:
        records = store['records']
        result['stores'].append(dict(file=store['file'], present=len(records), missing=len(store['missing']),
            fant=sum('fant' in r for r in records),
            categories=dict(collections.Counter(r['category'] for r in records)),
            actor_parts=sum(len(a['parts']) for r in records for a in r.get('actors', []))))
    result['visuals'] = dict(views=len(decoded['visuals']['views']),
                            graphics=len(decoded['visuals']['graphics']),
                            shade_depth_mismatches=decoded['visuals'].get('shade_map', {}).get('sphere_depth_mismatches'))
    return result


def cell(value):
    return str(value).replace('|', '\\|').replace('\n', ' ')


def vector(part, key):
    field = part['fields'].get(key)
    return ','.join(map(str, field['values'])) if field else '—'


def catalog(decoded):
    lines = ['# Ecstatica decoded actor and action catalogue', '',
        'Generated by `ecstatica_decode.py`; see [FORMAT.md](FORMAT.md) for interpretation.', '',
        'Vectors are signed raw file units, **halfaxes are radii**, rotations are raw angle words. '
        'No centimetre conversion or runtime pose solve is applied. Names preserve the game’s side-label inconsistencies. '
        'Offsets are relative to the beginning of the enclosing FANT record. `—` means no explicit field.', '']
    sources = [('CODE/ECSTATIC.FAN', decoded['main']['sha256'], decoded['main']['actors'])]
    for store in decoded['stores']:
        lines += [f'## {store["file"]} inventory', '', f'SHA-256: `{store["sha256"]}`.', '',
                  '| Category | Present | FANT |', '| --- | ---: | ---: |']
        for _, _, kind in RANGES:
            records = [r for r in store['records'] if r['category'] == kind]
            lines.append(f'| {kind} | {len(records)} | {sum("fant" in r for r in records)} |')
        lines.append('')
        for r in store['records']:
            if r.get('actors'):
                sources.append((f'{store["file"]} record {r["id"]}: {r["name"]} (container 0x{r["offset"]:x})',
                                r['sha256'], r['actors']))
    # Identical full/compact actors are documented once, with explicit references.
    seen = {}
    for label, sha, actor_list in sources:
        lines += [f'## {cell(label)}', '']
        if sha in seen:
            lines += [f'Byte-identical to {cell(seen[sha])}; SHA-256 `{sha}`.', '']
            continue
        seen[sha] = label
        lines += [f'SHA-256: `{sha}`.', '']
        for actor in actor_list:
            lines += [f'Actor slot {actor["slot"]}; ADD_THING at `0x{actor["offset"]:x}`.', '',
                '| ID / stored name | Parent ID | Halfaxes x,y,z | Attachment offset | Centre | Position | Rotation | Colour / flags operands | Type / display-point / vector3 | Halfaxes event |',
                '| --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- |']
            for p in actor['parts']:
                field = p['fields'].get('halfaxes')
                at = f'0x{field["offset"]:x}' if field else '—'
                lines.append(f'| {p["id"]}: {cell(p["name"])} | {p["parent"] if p["parent"] is not None else "root"} | '
                    f'{vector(p,"halfaxes")} | {vector(p,"offset")} | {vector(p,"centre")} | '
                    f'{vector(p,"position")} | {vector(p,"rotation")} | {vector(p,"colour")} / {vector(p,"flags_command")} | '
                    f'{vector(p,"type")} / {vector(p,"display_point")} / {vector(p,"vector3")} | `{at}` |')
            lines.append('')
            extra = [e for e in actor['events'] if e['opcode'] not in FIELDS and e['opcode'] not in (7, 8, 10)]
            if extra:
                lines += ['Actor settings, attachment points, triangle geometry and remaining commands:', '',
                          '| Offset | Opcode | Index | Three operands |', '| --- | --- | ---: | --- |']
                for e in extra:
                    lines.append(f'| `0x{e["offset"]:x}` | {e["name"]} | {e["index"]} | {",".join(map(str,e["values"]))} |')
                lines.append('')
    for store in decoded['stores']:
        lines += [f'## {store["file"]} actions', '',
                  'Header/key operands are preserved without assuming an FPS. Format: `index:value1,value2,value3`.', '',
                  '| Record / name | Action header / additional header | Key header(s), in file order | Constraint command counts |',
                  '| --- | --- | --- | --- |']
        for r in store['records']:
            if r['category'] != 'actions' or 'fant' not in r:
                continue
            es = r['fant']['action_events']
            def headers(op):
                return '; '.join(f'{e["index"]}:'+','.join(map(str,e['values'])) for e in es if e['opcode']==op)
            counts = collections.Counter(e['name'] for e in es if e['opcode'] in (28,29,30,32,33,34,35,48))
            lines.append(f'| {r["id"]}: {cell(r["name"])} | {headers(11)} / {headers(47)} | {headers(12)} | {dict(counts) if counts else "—"} |')
        lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, help='7z extraction directory')
    parser.add_argument('--json', type=Path, help='complete decoded events/payloads (keep game data outside repo)')
    parser.add_argument('--markdown', type=Path, help='all actor dimensions and action headers')
    args = parser.parse_args()
    try:
        decoded = extract(args.root)
        if args.json:
            args.json.write_text(json.dumps(decoded, separators=(',', ':')) + '\n')
        if args.markdown:
            args.markdown.write_text(catalog(decoded))
        print(json.dumps(summary(decoded), indent=2))
    except (DecodeError, OSError) as error:
        print(f'[ecstatica] {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
