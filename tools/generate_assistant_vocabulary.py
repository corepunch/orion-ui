#!/usr/bin/env python3
"""Package the assistant's language XML vocabularies as sandboxed Lua data.

Each vocabulary/<language>.xml lists words the way Zork's SYNTAX and SYNONYM
did. An entry with `means` defines a source word and what it means; any
language translates it with an entry naming the same `source`. `synonyms`
lists more headwords separated by spaces, like class= in HTML. A `paradigm`
from the file's <morphology> expands each headword into its forms, and a
<spelling> adds a variant of every form with one letter replaced.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

GROUPS = {'verbs', 'nouns', 'adjectives', 'fields', 'comparisons', 'relations', 'directions', 'topics'}
NUMBERS = {'ordinals', 'numbers', 'scales', 'multipliers'}
LISTS = {'pronouns', 'noise', 'units', 'number_conjunctions'}
# element: role. Group roles are keyed by a source word, number roles by `value`.
ELEMENTS = {
    'verb': 'verbs', 'noun': 'nouns', 'adjective': 'adjectives', 'field': 'fields',
    'comparison': 'comparisons', 'relation': 'relations', 'direction': 'directions',
    'ordinal': 'ordinals', 'number': 'numbers', 'scale': 'scales', 'multiplier': 'multipliers',
    'pronoun': 'pronouns', 'noise': 'noise', 'unit': 'units', 'conjunction': 'number_conjunctions',
    'topic': 'topics',
}
ENTRY_ATTRIBUTES = {'source', 'means', 'value', 'paradigm', 'synonyms', 'forms', 'compound', 'prefix'}


def normalization(node, mappings, path):
    if node.attrib:
        raise ValueError(f'{path}: invalid normalization attributes')
    for entry in node:
        source, target = entry.get('source', ''), entry.get('target', '')
        if (entry.tag != 'map' or entry.attrib.keys() != {'source', 'target'} or len(entry)
                or not source or len(source) != len(target)):
            raise ValueError(f'{path}: normalization requires equally sized character maps')
        for original, replacement in zip(source, target):
            if original in mappings and mappings[original] != replacement:
                raise ValueError(f'{path}: conflicting normalization for {original!r}')
            mappings[original] = replacement


def compound_numbers(node, path):
    if node.attrib.keys() != {'conjunction', 'multiplier', 'scale'} or not all(node.attrib.values()):
        raise ValueError(f'{path}: compound numbers require conjunction, multiplier and scale')
    config = dict(node.attrib, ones={}, small={}, tens={}, excluded_prefixes=[], replacements=[])
    for entry in node:
        if (entry.tag != 'replace' or entry.attrib.keys() != {'source', 'target'} or len(entry)
                or not entry.get('source') or not entry.get('target')):
            raise ValueError(f'{path}: invalid compound spelling replacement')
        config['replacements'].append(dict(entry.attrib))
    return config


def morphology(node, path):
    """Paradigms cut `cut` letters and add one ending per slot: "=" is the
    headword, "0" the bare stem, "-" no form."""
    classes, paradigms, spellings = {}, {}, {}
    if node.attrib:
        raise ValueError(f'{path}: invalid morphology attributes')
    for entry in node:
        a = entry.attrib
        if entry.tag == 'class':
            slots = a.get('slots', '').split()
            if a.keys() != {'name', 'slots'} or not a['name'] or not slots:
                raise ValueError(f'{path}: a class needs a name and slots')
            classes[a['name']] = slots
        elif entry.tag == 'paradigm':
            name, cut = a.get('name', ''), a.get('cut', '0')
            if a.keys() - {'name', 'class', 'cut'} or a.get('class') not in classes:
                raise ValueError(f'{path}: paradigm {name!r} needs a class declared before it')
            if not name or name in paradigms or not cut.isascii() or not cut.isdecimal():
                raise ValueError(f'{path}: paradigm {name!r} needs a unique name and a whole cut')
            endings, slots = (entry.text or '').split(), classes[a['class']]
            if len(endings) != len(slots):
                raise ValueError(f'{path}: paradigm {name} has {len(endings)} endings '
                                 f'for the {len(slots)} slots of {a["class"]}')
            paradigms[name] = (int(cut), endings)
        elif entry.tag == 'spelling':
            if a.keys() != {'from', 'to'} or len(a['from']) != 1 or not a['to']:
                raise ValueError(f'{path}: a spelling replaces one letter')
            spellings[a['from']] = a['to']
        else:
            raise ValueError(f'{path}: unknown morphology element <{entry.tag}>')
    return paradigms, spellings


def inflect(paradigms, word, name, path):
    if name not in paradigms:
        raise ValueError(f'{path}: unknown paradigm {name} for {word!r}')
    cut, endings = paradigms[name]
    if cut >= len(word):
        raise ValueError(f'{path}: paradigm {name} cuts all of {word!r}')
    stem = word[:len(word) - cut]
    forms = [word if e == '=' else stem if e == '0' else stem + e for e in endings if e != '-']
    return list(dict.fromkeys(forms))


def generate(directory):
    """The Lua module text for the vocabulary files in a directory."""
    return lua_module(compile_vocabulary(directory)[0])


def compile_vocabulary(directory):
    """Returns the module data and, per language, the source words it translates."""
    paths = sorted(directory.glob('*.xml'))
    if not paths:
        raise ValueError(f'{directory}: no vocabulary XML files')
    files, languages, mappings, meanings = [], {}, {}, {}
    for path in paths:
        root = ET.parse(path).getroot()
        language = root.get('language')
        if root.tag != 'vocabulary' or not language or root.attrib.keys() != {'language'}:
            raise ValueError(f'{path}: expected <vocabulary language="...">')
        if language in languages:
            raise ValueError(f'{path}: duplicate language {language}')
        languages[language] = {}
        sections = {tag: root.findall(tag) for tag in ('compound-numbers', 'morphology')}
        if any(len(nodes) > 1 for nodes in sections.values()):
            raise ValueError(f'{path}: duplicate compound number or morphology section')
        compound = compound_numbers(sections['compound-numbers'][0], path) if sections['compound-numbers'] else None
        if compound is not None:
            languages[language]['compound_numbers'] = compound
        rules = morphology(sections['morphology'][0], path) if sections['morphology'] else ({}, {})
        entries = []
        for node in root:
            if node.tag == 'normalization':
                normalization(node, mappings, path)
            elif node.tag not in ('compound-numbers', 'morphology'):
                if node.tag not in ELEMENTS or node.attrib.keys() - ENTRY_ATTRIBUTES or len(node):
                    raise ValueError(f'{path}: unknown vocabulary entry <{node.tag}>')
                source, means = node.get('source'), node.get('means')
                if means is not None:
                    if ELEMENTS[node.tag] not in GROUPS or not source or not means:
                        raise ValueError(f'{path}: <{node.tag}> defines a meaning with a source and means')
                    if (node.tag, source) in meanings:
                        raise ValueError(f'{path}: <{node.tag} source="{source}"> is defined twice')
                    meanings[node.tag, source] = means
                entries.append(node)
        files.append((path, language, compound, rules, entries))

    def normalize(text):
        out = []
        for c in text:
            if 'A' <= c <= 'Z':
                c = c.lower()
            if c in '  ':
                c = ' '
            out.append(mappings.get(c, c))
        return ''.join(out)

    vocabulary, aliases, sources = {}, {}, {}
    for path, language, compound, (paradigms, spellings), entries in files:
        covered = sources.setdefault(language, set())
        local_numbers = {'multipliers': {}, 'scales': {}}
        for node in entries:
            role, a = ELEMENTS[node.tag], node.attrib
            head = (node.text or '').strip() or (a.get('source', '') if 'means' in a else '')
            if not head:
                raise ValueError(f'{path}: <{node.tag}> needs a word')
            if role in GROUPS:
                if not a.get('source') or 'value' in a:
                    raise ValueError(f'{path}: <{node.tag}> {head!r} needs a source word')
                value = meanings.get((node.tag, a['source']))
                if value is None:
                    raise ValueError(f'{path}: <{node.tag} source="{a["source"]}"> is not defined by any means=')
                covered.add(f'{node.tag} {a["source"]}')
            elif role in NUMBERS:
                text = a.get('value', '')
                if 'source' in a or not text.isascii() or not text.isdecimal():
                    raise ValueError(f'{path}: <{node.tag}> {head!r} requires an integer value')
                value = int(text)
                if value > 2**63 - 1 or (value == 0 and role != 'numbers'):
                    raise ValueError(f'{path}: invalid {role} value {value}')
            else:
                if 'source' in a or 'value' in a:
                    raise ValueError(f'{path}: <{node.tag}> takes no source or value')
                value = True
            if 'compound' in a or 'prefix' in a:
                category = a.get('compound')
                if (role != 'numbers' or compound is None or category not in {'ones', 'small', 'tens'}
                        or a.get('prefix', 'true') not in {'true', 'false'}
                        or ('prefix' in a and category != 'ones')
                        or a.keys() & {'synonyms', 'forms', 'paradigm'}):
                    raise ValueError(f'{path}: invalid compound number {head!r}')
                valid = {'ones': 1 <= value <= 9, 'small': value == 0 or 10 <= value <= 19,
                         'tens': 20 <= value <= 90 and value % 10 == 0}
                if not valid[category]:
                    raise ValueError(f'{path}: invalid {category} value {value}')
                compound[category][normalize(head)] = value
                if a.get('prefix') == 'false':
                    compound['excluded_prefixes'].append(normalize(head))
            words = []
            for word in [head] + a.get('synonyms', '').split():
                word = normalize(word)
                words += inflect(paradigms, word, a['paradigm'], path) if 'paradigm' in a else [word]
            words += [normalize(form) for form in a.get('forms', '').split()]
            for word in words:
                spelled = ''.join(spellings.get(c, c) for c in word)
                for alias in dict.fromkeys((word, spelled)):
                    if role in local_numbers:
                        local_numbers[role][alias] = value
                    key = (role, alias)
                    if key in aliases and aliases[key] != value:
                        raise ValueError(f'{path}: conflicting {role} meanings for {alias!r}')
                    if key in aliases:
                        continue
                    aliases[key] = value
                    if role in LISTS:
                        vocabulary.setdefault(role, []).append(alias)
                    elif role in NUMBERS:
                        vocabulary.setdefault(role, {})[alias] = value
                    else:
                        vocabulary.setdefault(role, {}).setdefault(value, []).append(alias)
        if compound is not None:
            for role, key in (('multipliers', 'multiplier'), ('scales', 'scale')):
                value = local_numbers[role].get(compound[key])
                if value is None:
                    raise ValueError(f'{path}: compound {key} must name an alias in {role}')
                compound[key + '_value'] = value
            if not all(compound[key] for key in ('ones', 'small', 'tens')):
                raise ValueError(f'{path}: compound numbers require ones, small and tens numbers')
    defined = {f'{tag} {source}' for tag, source in meanings}
    data = {'lexicon': vocabulary, 'languages': languages, 'normalization': mappings}
    return data, {language: sorted(defined - covered) for language, covered in sources.items()}


def lua_module(data):
    def lua(value):
        if isinstance(value, str):
            return json.dumps(value, ensure_ascii=False)
        if isinstance(value, bool):
            return 'true' if value else 'false'
        if isinstance(value, int):
            return str(value)
        if isinstance(value, list):
            return '{' + ', '.join(lua(item) for item in value) + '}'
        return '{\n' + ''.join(f'  [{lua(key)}] = {lua(item)},\n'
                                 for key, item in sorted(value.items())) + '}'
    return '-- Generated from assistant/vocabulary/*.xml; do not edit.\nreturn ' + lua(data) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('directory', type=Path)
    parser.add_argument('--lua', type=Path, required=True)
    args = parser.parse_args()
    try:
        content = generate(args.directory)
    except (ValueError, ET.ParseError) as error:
        parser.exit(1, f'{error}\n')
    args.lua.parent.mkdir(parents=True, exist_ok=True)
    args.lua.write_text(content, encoding='utf-8')


if __name__ == '__main__':
    main()
