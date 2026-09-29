"""Read character/action streams directly from Ecstatica 1/2 ISO 9660 discs.

Only the FANT prefix (names, action events, actor events) is interpreted.
Unknown opcodes remain numeric events. Later scene/script/audio data is not
needed for character conversion and is recorded as an unparsed byte count.
"""
import collections
import contextlib
import hashlib
from pathlib import Path
import struct
from ecstatica_decode import DecodeError, Reader, NAMES, OPCODES, FIELDS, actors, lookup

BLOCK = 2048
MAX_MEMBER = 256 * 1024 * 1024


class Iso9660:
    def __init__(self, path):
        self.path = Path(path)
        self.file = self.path.open('rb')
        self.size = self.path.stat().st_size
        self.members = {}
        try:
            for sector in range(16, 80):
                desc = self._read(sector * BLOCK, BLOCK)
                if desc[1:6] != b'CD001' or desc[6] != 1:
                    raise DecodeError('expected a 2048-byte-sector ISO 9660 image')
                if desc[0] == 1:
                    if struct.unpack_from('<H', desc, 128)[0] != BLOCK:
                        raise DecodeError('unsupported ISO logical block size')
                    rec = desc[156:190]
                    self._directory(*self._extent(rec), '', set(), 0)
                    break
                if desc[0] == 255:
                    raise DecodeError('ISO has no primary volume descriptor')
            else:
                raise DecodeError('ISO primary descriptor not found')
        except Exception:
            self.close()
            raise

    def close(self):
        self.file.close()

    def _read(self, offset, size):
        if offset < 0 or size < 0 or size > MAX_MEMBER or offset + size > self.size:
            raise DecodeError(f'ISO extent outside image: {offset}+{size}')
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise DecodeError('truncated ISO extent')
        return data

    @staticmethod
    def _extent(rec):
        if len(rec) < 34:
            raise DecodeError('short ISO directory record')
        extent, size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        if extent != struct.unpack_from('>I', rec, 6)[0] or size != struct.unpack_from('>I', rec, 14)[0]:
            raise DecodeError('inconsistent ISO both-endian extent')
        if rec[25] & 128 or rec[26] or rec[27]:
            raise DecodeError('multi-extent/interleaved ISO members are unsupported')
        return extent * BLOCK, size

    def _directory(self, offset, size, prefix, seen, depth):
        if depth > 16 or size > 8 * 1024 * 1024 or offset in seen:
            raise DecodeError('cyclic or excessive ISO directory')
        seen = seen | {offset}
        data = self._read(offset, size)
        pos = 0
        while pos < size:
            length = data[pos]
            if not length:
                pos = (pos // BLOCK + 1) * BLOCK
                continue
            if length < 34 or pos + length > size:
                raise DecodeError('invalid ISO directory record length')
            rec = data[pos:pos + length]
            pos += length
            n = rec[32]
            if 33 + n > length:
                raise DecodeError('ISO filename overrun')
            raw = rec[33:33+n]
            if raw in (b'\0', b'\1'):
                continue
            name = raw.decode('ascii').split(';', 1)[0].rstrip('.').upper()
            if not name or name in ('.', '..') or '/' in name or '\\' in name:
                raise DecodeError('invalid ISO member name')
            path = prefix + name
            extent = self._extent(rec)
            if rec[25] & 2:
                self._directory(*extent, path + '/', seen, depth + 1)
            else:
                if path in self.members:
                    raise DecodeError(f'duplicate ISO member: {path}')
                self.members[path] = extent

    def read(self, name):
        try:
            extent = self.members[name.upper()]
        except KeyError as exc:
            raise DecodeError(f'missing disc member: {name}') from exc
        return self._read(*extent)


def event_stream(reader, version):
    events = []
    limit = 67 if version == 30 else 79
    while True:
        start = reader.pos
        op, index, *values = reader.unpack('>5h')
        if not 0 <= op < limit:
            raise DecodeError(f'unsupported v{version} opcode {op} at 0x{start:x}')
        event = dict(offset=start, opcode=op, index=index, values=values,
                     name=OPCODES[op] if op < len(OPCODES) else f'UNKNOWN_{op}')
        if op == 27 and index:
            event['attachment'] = reader.string()
        events.append(event)
        if not op:
            return events


def prefix(data):
    r = Reader(data)
    if r.take(4) != b'FANT':
        raise DecodeError('missing FANT magic')
    version, skip = r.unpack('>2h')
    if version not in (30, 55) or skip not in (0, 1):
        raise DecodeError(f'unsupported FANT version/skip {version}/{skip}')
    result = dict(version=version, reserved_hex=r.take(26).hex())
    if not skip:
        result['names'] = {}
        for category in NAMES + (['textures'] if version == 55 else []):
            values = []
            while True:
                value = r.string()
                if not value:
                    break
                values.append(value)
            result['names'][category] = values
    result['action_events'] = event_stream(r, version)
    result['actor_events'] = event_stream(r, version)
    result['prefix_bytes'] = r.pos
    result['unparsed_tail_bytes'] = len(data) - r.pos
    return result


def action_defs(events, names):
    result, current, key = [], None, None
    for event in events:
        op = event['opcode']
        if op == 11:
            current = dict(id=event['index'], name=lookup(names['actions'], event['index']),
                           headers=[], keys=[], events=[])
            result.append(current)
            key = None
        if current is None or op == 0:
            continue
        current['events'].append(event)
        if op == 12:
            phase = event['values'][0] & 65535
            if current['keys'] and phase < current['keys'][-1]['phase']:
                raise DecodeError(f'non-monotonic action {current["id"]} phase')
            key = dict(phase=phase, events=[])
            current['keys'].append(key)
        elif key is not None:
            key['events'].append(event)
        else:
            current['headers'].append(event)
    return result


def read_disc(path):
    with contextlib.closing(Iso9660(path)) as iso:
        main_data = iso.read('CODE/ECSTATIC.FAN')
        main = prefix(main_data)
        names = main.get('names')
        if not names:
            raise DecodeError('main FANT has no name tables')
        version = main['version']
        offsets_data, blob = iso.read('OFFSETS'), iso.read('FILES/ECSTATIC')
        expected = 3525 if version == 30 else 14060
        if len(offsets_data) != expected * 4:
            raise DecodeError(f'v{version} requires {expected} offsets')
        offsets = struct.unpack(f'>{expected}I', offsets_data)
        occupied = sorted(set(o for o in offsets if o != 0xffffffff))
        if not occupied or occupied[-1] >= len(blob):
            raise DecodeError('record offset outside container')
        ends = dict(zip(occupied, occupied[1:] + [len(blob)]))
        result = dict(game='ecstatica1' if version == 30 else 'ecstatica2', version=version,
                      iso_name=Path(path).name, names=names, actors=[], actions=[],
                      records=[], repertoires=[], members=len(iso.members),
                      source_sha256={name: hashlib.sha256(data).hexdigest() for name, data in
                          [('CODE/ECSTATIC.FAN', main_data), ('OFFSETS', offsets_data), ('FILES/ECSTATIC', blob)]})
        for index, offset in enumerate(offsets):
            if offset == 0xffffffff:
                continue
            data = blob[offset:ends[offset]]
            record = dict(record=index, offset=offset, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            if data.startswith(b'FANT'):
                decoded = prefix(data)
                if decoded['version'] != version:
                    raise DecodeError('mixed FANT versions in container')
                record['prefix_bytes'] = decoded['prefix_bytes']
                record['unparsed_tail_bytes'] = decoded['unparsed_tail_bytes']
                tail = Reader(data)
                tail.pos = decoded['prefix_bytes']
                event_stream(tail, version)
                code_count = tail.unpack('>h')
                if code_count == 0:
                    repertoire = None
                    for event in event_stream(tail, version):
                        if event['opcode'] == 53:
                            repertoire = dict(id=event['index'], entries=[], record=index)
                            result['repertoires'].append(repertoire)
                        elif event['opcode'] == 54 and repertoire is not None:
                            repertoire['entries'].append(event['values'])
                for actor in actors(decoded['actor_events'], names):
                    actor.update(record=record, name=lookup(names['actors'], actor['slot']))
                    result['actors'].append(actor)
                for action in action_defs(decoded['action_events'], names):
                    action['record'] = record
                    result['actions'].append(action)
            else:
                record['opaque'] = True
            result['records'].append(record)
        return result
