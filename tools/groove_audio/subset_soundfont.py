"""Keep selected SF2 presets and their unmodified instrument/sample dependencies."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def chunks(data):
    at = 0
    while at + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, at)
        end = at + 8 + size
        if end > len(data):
            raise ValueError('Truncated RIFF chunk')
        yield tag, data[at + 8:end]
        at = end + (size & 1)


def chunk(tag, data):
    return struct.pack('<4sI', tag, len(data)) + data + b'\0' * (len(data) & 1)


def subset(source, destination, presets):
    raw = source.read_bytes()
    if raw[:4] != b'RIFF' or raw[8:12] != b'sfbk':
        raise ValueError('Expected an uncompressed SF2 SoundFont')
    sections = {body[:4]: dict(chunks(body[4:])) for tag, body in chunks(raw[12:]) if tag == b'LIST'}
    if b'sm24' in sections[b'sdta'] or sections[b'sdta'][b'smpl'].startswith(b'OggS'):
        raise ValueError('This subsetter preserves uncompressed 16-bit PCM samples only')
    formats = {'phdr': '<20sHHHIII', 'pbag': '<HH', 'pmod': '<HHhHH', 'pgen': '<HH',
               'inst': '<20sH', 'ibag': '<HH', 'imod': '<HHhHH', 'igen': '<HH', 'shdr': '<20sIIIIIBbHH'}
    tables = {key: list(struct.iter_unpack(fmt, sections[b'pdta'][key.encode()])) for key, fmt in formats.items()}
    chosen = [i for i, row in enumerate(tables['phdr'][:-1]) if (row[2], row[1]) in presets]
    found = {(tables['phdr'][i][2], tables['phdr'][i][1]) for i in chosen}
    if found != presets:
        raise ValueError(f'Missing presets: {presets - found}')

    def dependencies(indices, header, field, bags, generators, reference):
        result = set()
        for index in indices:
            for bag in range(tables[header][index][field], tables[header][index + 1][field]):
                for op, value in tables[generators][tables[bags][bag][0]:tables[bags][bag + 1][0]]:
                    if op == reference:
                        result.add(value)
        return sorted(result)

    instruments = dependencies(chosen, 'phdr', 3, 'pbag', 'pgen', 41)
    samples = set(dependencies(instruments, 'inst', 1, 'ibag', 'igen', 53))
    # Preserve both sides of linked stereo samples even when only one is referenced.
    pending = list(samples)
    while pending:
        row = tables['shdr'][pending.pop()]
        if row[9] & 6 and row[8] not in samples:
            samples.add(row[8])
            pending.append(row[8])
    samples = sorted(samples)
    instrument_map = {old: new for new, old in enumerate(instruments)}
    sample_map = {old: new for new, old in enumerate(samples)}
    output = {key: [] for key in formats}

    def copy_zones(indices, header, field, bags, generators, modulators, reference, remap):
        for index in indices:
            head = list(tables[header][index])
            first, last = head[field], tables[header][index + 1][field]
            head[field] = len(output[bags])
            output[header].append(head)
            for bag in range(first, last):
                gen, mod = tables[bags][bag]
                gen_end, mod_end = tables[bags][bag + 1]
                output[bags].append((len(output[generators]), len(output[modulators])))
                for op, value in tables[generators][gen:gen_end]:
                    output[generators].append((op, remap[value] if op == reference else value))
                output[modulators].extend(tables[modulators][mod:mod_end])
        terminal = list(tables[header][-1])
        terminal[field] = len(output[bags])
        output[header].append(terminal)
        output[bags].append((len(output[generators]), len(output[modulators])))
        output[generators].append((0, 0))
        output[modulators].append((0, 0, 0, 0, 0))

    copy_zones(chosen, 'phdr', 3, 'pbag', 'pgen', 'pmod', 41, instrument_map)
    copy_zones(instruments, 'inst', 1, 'ibag', 'igen', 'imod', 53, sample_map)
    if any(op in (0, 1, 4, 12) and value for op, value in output['igen']):
        raise ValueError('Sample start/end offsets require a wider PCM dependency range')
    pcm = sections[b'sdta'][b'smpl']
    rendered = bytearray()
    for index in samples:
        row = list(tables['shdr'][index])
        start, end = row[1:3]
        if not 0 <= start < end <= len(pcm) // 2:
            raise ValueError(f'Invalid sample bounds: {index}')
        # Keep guard frames for the interpolator. No waveform processing occurs.
        offset = len(rendered) // 2 - start
        rendered.extend(pcm[start * 2:min(end + 46, len(pcm) // 2) * 2])
        rendered.extend(b'\0' * max(0, (end + 46 - len(pcm) // 2) * 2))
        for field in range(1, 5):
            row[field] += offset
        if row[9] & 6:
            row[8] = sample_map[row[8]]
        else:
            row[8] = 0
        output['shdr'].append(row)
    end = len(rendered) // 2
    output['shdr'].append((b'EOS', end, end, end, end, 0, 0, 0, 0, 1))
    info = b''.join(chunk(tag, body) for tag, body in sections[b'INFO'].items())
    pdta = b''.join(chunk(key.encode(), b''.join(struct.pack(fmt, *row) for row in output[key])) for key, fmt in formats.items())
    result = chunk(b'RIFF', b'sfbk' + chunk(b'LIST', b'INFO' + info)
                   + chunk(b'LIST', b'sdta' + chunk(b'smpl', rendered)) + chunk(b'LIST', b'pdta' + pdta))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(result)
    return {'source_sha256': hashlib.sha256(raw).hexdigest(), 'output_sha256': hashlib.sha256(result).hexdigest(),
            'bytes': len(result), 'instruments': len(instruments), 'samples': len(samples),
            'presets': [{'bank': tables['phdr'][i][2], 'program': tables['phdr'][i][1],
                         'name': tables['phdr'][i][0].rstrip(b'\0').decode('ascii')} for i in chosen]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--preset', action='append', required=True, help='zero-based BANK:PROGRAM')
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    report = subset(args.source, args.destination, {tuple(map(int, p.split(':'))) for p in args.preset})
    args.manifest.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Kept {len(report['presets'])} presets, {report['samples']} samples, {report['bytes']} bytes")
