"""Author/rebuild the three EuroDance demos; --check never changes saved projects."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBRARY_LEVELS = {cat: int(db) for cat, db in re.findall(r'^  \[(CAT_\w+)\]\s*=\s*\{\s*(-?\d+)', (ROOT / 'apps/groove/sample_levels.h').read_text(), re.M)}
# Measured outlier trims at 140 BPM; these placements retain the approved mix.
DEMO_SOURCE_LEVELS = {'Crash Down': -6, 'Club Lift Two': -1, 'Club Landing': -2}
RECIPES = []
SONGS = []
CHORDS = {
    'Am': 'A3+C4+E4', 'F': 'F3+A3+C4', 'C': 'G3+C4+E4',
    'G': 'G3+B3+D4', 'Dm': 'A3+D4+F4', 'Em': 'G3+B3+E4', 'E': 'G#3+B3+E4',
}
ROOTS = {'Am': 'A1', 'F': 'F1', 'C': 'C2', 'G': 'G1', 'Dm': 'D2', 'Em': 'E1', 'E': 'E1'}
FIFTHS = {'Am': 'E2', 'F': 'C2', 'C': 'G2', 'G': 'D2', 'Dm': 'A2', 'Em': 'B1', 'E': 'B1'}


def note(name, cat, inst, bars, spb, gate, pattern, fx='0'):
    pattern = ' '.join(pattern) if isinstance(pattern, list) else pattern
    assert len(pattern.split()) == bars * 4 * spb, (name, len(pattern.split()))
    RECIPES.append(dict(name=name, kind='N', cat=cat, inst=inst, bars=bars,
                        spb=spb, gate=gate, pattern=pattern, fx=fx))
    return name


def drum(name, bars, lanes, fill=False):
    assert all(len(v) == 16 * bars for v in lanes.values()), name
    if 'h' in lanes and 'o' in lanes:
        lanes['h'] = ''.join('.' if o != '.' else h for h, o in zip(lanes['h'], lanes['o']))
    for k in ('h', 'o'):
        assert set(lanes.get(k, '')) <= set('.o'), name
    RECIPES.append(dict(name=name, kind='D', cat='CAT_FILL' if fill else 'CAT_DRUMS',
                        inst='KIT_909', bars=bars, fx='0',
                        pattern=' '.join(f'{k}={v}' for k, v in lanes.items())))
    return name


def oct_up(n):
    return n[:-1] + str(int(n[-1]) + 1)


def lower(n):
    return re.sub(r'([A-G][#b]?)(\d)', lambda m: m[1] + str(int(m[2]) - 1), n)


def wide(bars):
    return [' '.join(t if t in ('-', '_') else lower(t) + '+' + t for t in b.split()) for b in bars]


def bass(prog, chorus=False):
    parts = []
    for i, ch in enumerate(prog):
        r, fifth = ROOTS[ch], FIFTHS[ch]
        if chorus:
            parts.append(f'- - {r}! - - - {oct_up(r)} - - - {fifth} - - - {r} -')
        else:
            parts.append(f'- {r} - {fifth} - {r} - {oct_up(r) if i % 2 else fifth}')
    return parts


def chords(prog, role):
    if role == 'keys':
        return [f'- {CHORDS[ch]} _ - - {CHORDS[ch]} - {CHORDS[ch]}' for ch in prog]
    if role == 'guitar':
        return [f'- {CHORDS[ch]} - - - {CHORDS[ch]} - -' for ch in prog]
    return [f'{CHORDS[ch]} _ _ _' for ch in prog]


# A continuous 909 floor; changes in density and the last bar create transitions.
kick = 'x...x...x...x...'
clap = '....x.......x...'
hats = 'o.o.o.o.o.o.o.o.'
opens = '..o.......o.....'
drum('Club Foundation', 4, {'k': kick * 4, 'c': clap * 4})
drum('Club Floor', 4, {'k': kick * 4, 'c': clap * 4, 'h': hats * 4, 'o': opens * 4})
drum('Club Motion', 4, {'k': kick * 3 + 'x...x...x...x.o.', 'c': clap * 4,
                       'h': hats * 3 + 'o.o.o.o.o.o.o...', 'o': opens * 4,
                       'g': '.......o........' + '...............o' + '...o............' + '.............o..'})
drum('Club Turnaround', 4, {'k': kick * 3 + 'x...x...x.......', 'c': clap * 3 + '....x...........',
                           'h': hats * 3 + 'o.o.o.o.........', 'o': opens * 3 + '.' * 16,
                           's': '.' * 48 + '........o.x.ox..',
                           't': '.' * 48 + '..............o.'})
drum('Club Lift One', 2, {'k': kick * 2, 's': 'o.......o.......' + 'o...o...x...x...', 'c': clap * 2}, True)
drum('Club Lift Two', 2, {'k': kick + 'x...x...........',
                         's': 'o...o...x...x...' + 'o.o.x.x.xxxx....'}, True)
drum('Club Pickup', 2, {'k': kick * 2, 'c': clap * 2, 'h': hats + 'o.o.o.o.........',
                       's': '.' * 16 + '........o.x.ox..'}, True)
drum('Club Landing', 4, {'k': kick * 2 + 'x...x...........' + '.' * 16,
                        'c': clap * 2 + '....o...........' + '.' * 16,
                        'h': hats * 2 + '.' * 32})

DATA = [
    dict(slug='neon-skyline', title='Neon Skyline', prefix='Skyline', reference='Jazz1', bars=88,
         verse=['Am', 'F', 'Dm', 'Em'], chorus=['Am', 'F', 'C', 'G'], lift=['Dm', 'Em', 'F', 'G'],
         hook_a=['- E5 A5 _ G5 _ E5 _', 'C5 _ A4 _ C5 D5 E5 _', 'G5 _ E5 _ D5 C5 _ _', 'B4 _ D5 _ E5 _ _ -'],
         hook_b=['C5 E5 A5 _ _ G5 E5 _', 'F5 _ E5 D5 C5 _ A4 _', 'E5 _ G5 _ A5 G5 E5 _', 'D5 _ B4 _ A4 _ _ -'],
         answer=['E4 _ A4 _ C5 B4 A4 _', 'A4 _ C5 _ G4 _ F4 _', 'F4 _ A4 _ D5 C5 A4 _', 'G4 _ B4 _ E5 D5 B4 _'],
         quiet=['A4 _ _ _ E5 _ C5 _', 'C5 _ _ _ A4 _ _ -', 'G4 _ _ _ E4 _ G4 _', 'D5 _ B4 _ A4 _ _ -'],
         signal=['E5 _ _ - A4 _ _ -', 'C5 _ A4 _ _ _ - -', 'G4 _ _ - E5 _ _ -', 'D5 _ B4 _ _ _ - -']),
    dict(slug='afterglow-avenue', title='Afterglow Avenue', prefix='Afterglow', reference='Jazz2', bars=88,
         verse=['Am', 'Dm', 'F', 'Em'], chorus=['C', 'G', 'Am', 'F'], lift=['Dm', 'F', 'G', 'Em'],
         hook_a=['- G4 C5 _ E5 _ G5 _', 'D5 _ B4 _ A4 B4 D5 _', 'E5 _ A5 _ G5 E5 C5 _', 'C5 _ A4 _ G4 _ _ -'],
         hook_b=['E5 _ G5 A5 _ G5 E5 _', 'D5 _ G5 _ F5 D5 B4 _', 'C5 E5 A5 _ _ E5 C5 _', 'A4 _ C5 _ D5 C5 A4 _'],
         answer=['- E4 G4 _ A4 _ C5 _', '- F4 A4 _ D5 _ A4 _', 'C5 _ A4 _ G4 A4 C5 _', 'B4 _ G4 E4 _ _ - -'],
         quiet=['C5 _ _ _ G4 _ E4 _', 'B4 _ _ _ D5 _ _ -', 'C5 _ E5 _ A4 _ _ -', 'A4 _ C5 _ G4 _ _ -'],
         signal=['G4 _ C5 _ _ _ - -', 'D5 _ B4 _ _ _ - -', 'E5 _ _ - C5 _ _ -', 'A4 _ G4 _ _ _ - -']),
    dict(slug='victory-lights', title='Victory Lights', prefix='Victory', reference='RAPTOR seventh mission', bars=96,
         verse=['Am', 'Am', 'F', 'G'], chorus=['Am', 'F', 'C', 'G'], lift=['Dm', 'F', 'Em', 'E'],
         hook_a=['A4 _ _ E5 G5 _ E5 _', 'F5 _ E5 D5 C5 _ _ -', 'E5 _ G5 _ C5 E5 G5 _', 'D5 _ _ B4 A4 _ _ -'],
         hook_b=['E5 _ A5 _ G5 E5 C5 _', 'A4 _ C5 _ F5 _ E5 D5', 'G5 _ E5 _ C5 _ D5 E5', 'B4 _ D5 _ A4 _ _ -'],
         answer=['A3 _ E4 _ A4 _ G4 E4', 'C4 _ E4 _ A4 _ _ -', 'A3 _ C4 _ F4 _ E4 C4', 'B3 _ D4 _ G4 _ D4 _'],
         quiet=['E5 _ _ _ C5 _ A4 _', 'A4 _ _ _ C5 _ _ -', 'G4 _ _ _ E5 _ C5 _', 'B4 _ D5 _ A4 _ _ -'],
         signal=['A4 _ _ - E5 _ _ -', 'C5 _ _ _ _ _ - -', 'E5 _ C5 _ _ _ - -', 'D5 _ B4 _ A4 _ _ -']),
]

for d in DATA:
    p = d['prefix']
    d['parts'] = {}
    def add(role, cat, inst, pat, spb=2, gate=1.4, fx='0'):
        d['parts'][role] = note(f'{p} {role}', cat, inst, 4, spb, gate, pat, fx)
    add('Verse Bass', 'CAT_BASS', 'I_BASS', bass(d['verse']), gate=0.78)
    add('Chorus Bass', 'CAT_BASS', 'I_BASS', bass(d['chorus'], True), spb=4, gate=1.1)
    add('Lift Bass', 'CAT_BASS', 'I_BASS', bass(d['lift']), gate=0.85)
    add('Verse Piano', 'CAT_KEYS', 'I_HPIANO', chords(d['verse'], 'keys'), gate=1.0, fx='X_ROOM')
    add('Lift Piano', 'CAT_KEYS', 'I_HPIANO', chords(d['lift'], 'keys'), gate=1.3, fx='X_ROOM')
    add('Lift Guitar', 'CAT_GUITAR', 'I_CLEAN', chords(d['lift'], 'guitar'), gate=0.8, fx='X_ROOM')
    add('Hook A', 'CAT_KEYS', 'I_HPIANO', d['hook_a'], gate=1.65, fx='X_ROOM')
    add('Hook B', 'CAT_KEYS', 'I_HPIANO', d['hook_b'], gate=1.65, fx='X_ROOM')
    add('Final A', 'CAT_KEYS', 'I_HPIANO', wide(d['hook_a']), gate=1.7, fx='X_ROOM')
    add('Final B', 'CAT_KEYS', 'I_HPIANO', wide(d['hook_b']), gate=1.7, fx='X_ROOM')
    add('Break Piano', 'CAT_KEYS', 'I_PIANO', d['quiet'], gate=1.8, fx='X_ROOM')
    add('Break Guitar', 'CAT_GUITAR', 'I_CLEAN', [lower(b) for b in d['quiet']], gate=1.8, fx='X_ROOM')
    build_bass = bass(d['lift'])
    build_bass[-1] = ' '.join(build_bass[-1].split()[:6] + ['-', '-'])
    build_keys = chords(d['lift'], 'keys')
    build_keys[-1] = ' '.join(build_keys[-1].split()[:6] + ['-', '-'])
    add('Build Bass', 'CAT_BASS', 'I_BASS', build_bass, gate=0.8)
    add('Build Piano', 'CAT_KEYS', 'I_HPIANO', build_keys, gate=1.0, fx='X_ROOM')
    add('Signal', 'CAT_KEYS', 'I_PIANO', d['signal'], gate=1.75, fx='X_ROOM | X_UP')
    add('Verse Guitar', 'CAT_GUITAR', 'I_WAH' if p == 'Afterglow' else 'I_CLEAN', chords(d['verse'], 'guitar'), gate=0.7, fx='X_ROOM')
    add('Chorus Guitar', 'CAT_GUITAR', 'I_PLUCK', chords(d['chorus'], 'held'), spb=1, gate=0.95, fx='X_ROOM')
    add('Guitar Answer', 'CAT_GUITAR', 'I_CLEAN', d['answer'], gate=1.7, fx='X_ROOM')
    for role, prog in [('Verse Pad', d['verse']), ('Chorus Pad', d['chorus']), ('Lift Pad', d['lift'])]:
        add(role, 'CAT_PAD', 'I_PAD', chords(prog, 'held'), spb=1, gate=0.92, fx='X_CHORUS | X_ROOM')
    add('Final Chords', 'CAT_SYNTH', 'I_SUPERSAW', chords(d['chorus'], 'held'), spb=1, gate=0.88, fx='X_PUMP | X_ROOM')
    close = ['Am', 'F', 'Em', 'Am']
    closing_bass = bass(close)
    closing_bass[-1] = 'A1 _ _ _ - - - -'
    add('Closing Bass', 'CAT_BASS', 'I_BASS', closing_bass, gate=0.9)
    endings = {
        'Skyline': ['C5 _ E5 _ A4 _ _ -', 'C5 _ A4 _ F4 _ _ -', 'B4 _ E5 D5 B4 _ G4 _', 'A4 _ _ - - - - -'],
        'Afterglow': ['E5 _ C5 _ A4 _ _ -', 'A4 _ C5 _ A4 G4 F4 _', 'E4 G4 B4 _ D5 _ B4 _', 'C5 _ A4 _ - - - -'],
        'Victory': ['E5 _ C5 A4 _ _ _ -', 'C5 _ F5 _ E5 D5 C5 _', 'B4 _ E5 _ D5 B4 G4 _', 'A4+E5 _ _ - - - - -'],
    }
    close_guitar = chords(close, 'held')
    close_guitar[-1] = CHORDS['Am'] + ' _ - -'
    add('Closing Piano', 'CAT_KEYS', 'I_PIANO', endings[p], gate=1.7, fx='X_ROOM')
    add('Closing Guitar', 'CAT_GUITAR', 'I_PLUCK', close_guitar, spb=1, gate=0.9, fx='X_ROOM')

    song = dict(slug=d['slug'], title=d['title'], bpm=140, bars=d['bars'], reference=d['reference'], clips=[], sections=[])
    SONGS.append(song)
    parts = d['parts']
    def put(name, track, bar, span=None):
        # Mix the arrangement after source levelling; leave library previews and timbres intact.
        levels = {'Verse Piano': -7, 'Lift Piano': -6, 'Build Piano': -6,
                  'Hook A': -5, 'Hook B': -5, 'Final A': -6, 'Final B': -6,
                  'Break Piano': -3, 'Closing Piano': -5, 'Signal': -5,
                  'Verse Guitar': -2, 'Chorus Guitar': -2, 'Lift Guitar': -2,
                  'Guitar Answer': -3, 'Break Guitar': -2, 'Closing Guitar': -2,
                  'Verse Pad': -4, 'Chorus Pad': -4, 'Lift Pad': -4, 'Final Chords': -5,
                  'Riser': -3, 'Crash Down': -3, 'Come On': -2, "Let's Go": -2, 'Hey Yo': -2}
        song['clips'].append(dict(name=parts.get(name, name), track=track, bar=bar, span=span, gain_db=levels.get(name, 0)))
    def cell(bar, title, rhythm=None, bass=None, keys=None, guitar=None, pad=None, chords_=None):
        song['sections'].append(dict(bar=bar, title=title))
        for t, n in enumerate([rhythm, bass, keys, guitar, pad, chords_]):
            if n: put(n, t, bar)
    # Lanes: drums, sampled bass, piano hook/chords, guitar, pad, synth chords, reserved, FX/calls.
    cell(0, 'Filtered signal', 'Club Foundation', keys='Signal', pad='Chorus Pad')
    cell(4, 'Bass and guitar entrance', 'Club Floor', bass='Verse Bass', guitar='Verse Guitar')
    cell(8, 'Verse: piano rhythm', 'Club Floor', bass='Verse Bass', keys='Verse Piano', guitar='Verse Guitar')
    cell(12, 'Verse: guitar answer', 'Club Motion', bass='Verse Bass', keys='Verse Piano')
    put('Guitar Answer', 6, 12)
    cell(16, 'Verse develops in the bass', 'Club Floor', bass='Verse Bass', keys='Verse Piano', pad='Verse Pad')
    cell(20, 'Verse closes into the lift', 'Club Turnaround', bass='Verse Bass', guitar='Verse Guitar', pad='Verse Pad')
    cell(24, 'Pre-chorus harmony rises', 'Club Foundation', bass='Lift Bass', keys='Lift Piano', pad='Lift Pad')
    cell(28, 'Snare build and breath', bass='Build Bass', keys='Build Piano')
    put('Club Lift One', 0, 28); put('Club Lift Two', 0, 30); put('Riser', 7, 30)
    # The two four-bar halves form a single hook, rather than a string of unrelated game-style leads.
    cell(32, 'Chorus: main hook', 'Club Floor', bass='Chorus Bass', keys='Hook A', guitar='Chorus Guitar')
    cell(36, 'Chorus: answering hook', 'Club Motion', bass='Chorus Bass', keys='Hook B', pad='Chorus Pad')
    cell(40, 'Chorus gains guitar and pad', 'Club Floor', bass='Chorus Bass', keys='Hook A', guitar='Chorus Guitar', pad='Chorus Pad')
    cell(44, 'Chorus turns toward the break', 'Club Turnaround', bass='Chorus Bass', keys='Hook B', guitar='Chorus Guitar')
    cell(48, 'Piano-led breakdown', keys='Break Piano', pad='Chorus Pad')
    cell(52, 'Guitar replies in the breakdown', guitar='Chorus Guitar', pad='Chorus Pad')
    put('Break Guitar', 6, 52)
    rebuild = 56
    if p == 'Victory':
        cell(56, 'Heroic middle passage', keys='Break Piano', chords_='Final Chords')
        cell(60, 'Low pulse gathers momentum', 'Club Foundation', bass='Chorus Bass', guitar='Chorus Guitar')
        rebuild = 64
    cell(rebuild, 'Kick returns under rising chords', 'Club Foundation', bass='Lift Bass', keys='Lift Piano', guitar='Lift Guitar', pad='Lift Pad')
    cell(rebuild + 4, 'Final build and dropout', bass='Build Bass', keys='Build Piano')
    put('Club Lift One', 0, rebuild + 4); put('Club Lift Two', 0, rebuild + 6); put('Riser', 7, rebuild + 6)
    finale = rebuild + 8
    cell(finale, 'Final chorus: octave lift', 'Club Motion', bass='Chorus Bass', keys='Final A', guitar='Chorus Guitar', pad='Chorus Pad')
    cell(finale + 4, 'Final chorus: wide answer', 'Club Floor', bass='Chorus Bass', keys='Final B', guitar='Chorus Guitar', chords_='Final Chords')
    cell(finale + 8, 'Final chorus: original hook returns', 'Club Motion', bass='Chorus Bass', keys='Hook A', pad='Chorus Pad', chords_='Final Chords')
    cell(finale + 12, 'Final chorus: closing answer', 'Club Turnaround', bass='Chorus Bass', keys='Final B', pad='Chorus Pad')
    cell(finale + 16, 'DJ outro: rhythm and guitar', 'Club Floor', bass='Chorus Bass', guitar='Chorus Guitar')
    cell(finale + 20, 'Cadence: instruments withdraw', 'Club Landing', bass='Closing Bass', keys='Closing Piano', guitar='Closing Guitar')
    # Short, unpitched/rap calls retain their native 140-BPM pitch and time; the hook remains foreground.
    for name, bar in [('Come On', 7), ("Let's Go", 31), ('Hey Yo', rebuild + 7)]:
        # Keep rising FX and a call separate so neither silently crops the other.
        put(name, 6, bar)
    put('Crash Down', 7, 32); put('Crash Down', 7, finale)


def generate():
    base = (ROOT / 'apps/groove/library.c').read_text().split('static const def_t kDefs[] = {', 1)[1].split('\n};', 1)[0]
    old = []
    for m in re.finditer(r'^  (\w+)\("([^"]+)"(.*)', base, re.M):
        args = m[3].split(','); offset = 2 if m[1] in ('NV', 'DV') else -1 if m[1] == 'M' else 0
        old.append(dict(name=m[2], bars=int(args[3 + offset]), cat='CAT_VOX' if m[1] == 'M' else args[1 + offset].strip()))
    assert len(old) == 500, 'The base catalogue changed: reconcile IDs before regenerating.'
    catalog = old + RECIPES
    lookup = {r['name']: dict(r, id=i) for i, r in enumerate(catalog)}
    assert len(lookup) == len(catalog)
    lines = ['// Original EuroDance demo recipes; included at the end of kDefs.', '// Generated by tools/groove_audio/compose_eurodance_demos.py --write.']
    for r in RECIPES:
        if r['kind'] == 'N':
            lines.append(f'  N("{r["name"]}", {r["cat"]}, DAN, {r["bars"]}, {r["inst"]}, {r["fx"]}, {r["spb"]}, {r["gate"]:.2f}f, "{r["pattern"]}"),')
        else:
            lines.append(f'  D("{r["name"]}", {r["cat"]}, DAN, {r["bars"]}, {r["inst"]}, {r["fx"]}, "{r["pattern"]}"),')
    files = {ROOT / 'apps/groove/demo_phrases.h': '\n'.join(lines) + '\n'}
    for s in SONGS:
        clips = sorted(s['clips'], key=lambda c: (c['bar'], c['track']))
        for c in clips:
            b = lookup[c['name']]; c['id'] = b['id']; c['bars'] = b['bars'] if c['span'] is None else c['span']
            c['mix_gain_db'] = c['gain_db']
            c['gain_db'] -= DEMO_SOURCE_LEVELS.get(c['name'], LIBRARY_LEVELS[b['cat']])
            assert 0 <= c['track'] < 8 and 0 <= c['bar'] and c['bar'] + c['bars'] <= s['bars']
        assert len(clips) <= 256
        for i, a in enumerate(clips):
            for b in clips[i + 1:]:
                assert a['track'] != b['track'] or min(a['bar'] + a['bars'], b['bar'] + b['bars']) <= max(a['bar'], b['bar']), (s['title'], a, b)
        assert max(c['bar'] + c['bars'] for c in clips) == s['bars']
        ids = list(dict.fromkeys(c['id'] for c in clips))
        text = ['ORION_GROOVE 4', f'140 0 0 0 {len(ids)} {len(clips)}']
        text += [f'B {i}\t{catalog[i]["name"]}' for i in ids]
        text += [f'{ids.index(c["id"])} {c["track"]} {c["bar"] * 256} {i} {0 if c["bars"] == catalog[c["id"]]["bars"] else c["bars"] * 256} {c["gain_db"]}' for i, c in enumerate(clips, 1)]
        files[ROOT / 'apps/groove/share/demos' / (s['slug'] + '.groove')] = '\n'.join(text) + '\n'
        s['clips'] = clips; s['sources'] = len(ids)
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='Replace the authored demo recipes and bundled projects.')
    parser.add_argument('--check', action='store_true', help='Check generated content without writing.')
    args = parser.parse_args()
    files = generate()
    for path, text in files.items():
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True); path.write_text(text)
        else:
            assert path.exists() and path.read_text() == text, f'Generated content differs: {path}'
    report = ROOT / 'build/groove-eurodance'
    report.mkdir(parents=True, exist_ok=True)
    (report / 'scores.json').write_text(json.dumps(dict(recipes=RECIPES, songs=SONGS), indent=2) + '\n')
    print(f'{len(RECIPES)} original recipes; ' + ', '.join(f'{s["title"]}: {s["bars"]} bars, {len(s["clips"])} clips' for s in SONGS))
