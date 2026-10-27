#!/usr/bin/env python3
"""Package the assistant's language XML vocabularies as sandboxed Lua data."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

GROUPS = {'verbs', 'nouns', 'adjectives', 'fields', 'comparisons', 'relations', 'directions'}
NUMBERS = {'ordinals', 'numbers', 'scales', 'multipliers'}
LISTS = {'pronouns', 'noise', 'units', 'number_conjunctions'}


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


def generate(directory):
    vocabulary, meanings, languages, mappings = {}, {}, {}, {}
    paths = sorted(directory.glob('*.xml'))
    if not paths:
        raise ValueError(f'{directory}: no vocabulary XML files')
    for path in paths:
        root = ET.parse(path).getroot()
        language = root.get('language')
        if root.tag != 'vocabulary' or not language or root.attrib.keys() != {'language'}:
            raise ValueError(f'{path}: expected <vocabulary language="...">')
        if language in languages:
            raise ValueError(f'{path}: duplicate language {language}')
        languages[language] = {}
        compounds = root.findall('compound-numbers')
        if len(compounds) > 1:
            raise ValueError(f'{path}: duplicate compound number configuration')
        compound = compound_numbers(compounds[0], path) if compounds else None
        if compound is not None:
            languages[language]['compound_numbers'] = compound
        local_numbers = {'multipliers': {}, 'scales': {}}
        for group in root:
            if group.tag == 'compound-numbers':
                continue
            if group.tag == 'normalization':
                normalization(group, mappings, path)
                continue
            role, value = group.get('role'), group.get('value')
            if group.tag != 'aliases' or role not in GROUPS | NUMBERS | LISTS:
                raise ValueError(f'{path}: unknown vocabulary role {role}')
            attrs = {'role'} if role in LISTS else {'role', 'value'}
            if group.attrib.keys() != attrs or not len(group):
                raise ValueError(f'{path}: invalid alias group {role}')
            if role in NUMBERS:
                if not value or not value.isascii() or not value.isdecimal():
                    raise ValueError(f'{path}: {role} requires an integer value')
                value = int(value)
                if value > 2**63 - 1 or (value == 0 and role != 'numbers'):
                    raise ValueError(f'{path}: invalid {role} value {value}')
            elif role in LISTS:
                value = True
            elif not value:
                raise ValueError(f'{path}: missing {role} meaning')
            values = vocabulary.setdefault(role, {} if role not in LISTS else [])
            for node in group:
                alias = (node.text or '').strip()
                if node.tag != 'alias' or node.attrib.keys() - {'compound', 'prefix'} or len(node) or not alias:
                    raise ValueError(f'{path}: expected a nonempty <alias>')
                category = node.get('compound')
                if node.attrib:
                    if (role != 'numbers' or compound is None or category not in {'ones', 'small', 'tens'}
                            or node.get('prefix', 'true') not in {'true', 'false'}
                            or ('prefix' in node.attrib and category != 'ones')):
                        raise ValueError(f'{path}: invalid compound number alias {alias!r}')
                    valid = {'ones': 1 <= value <= 9, 'small': value == 0 or 10 <= value <= 19,
                             'tens': 20 <= value <= 90 and value % 10 == 0}
                    if not valid[category]:
                        raise ValueError(f'{path}: invalid {category} value {value}')
                    compound[category][alias] = value
                    if node.get('prefix') == 'false':
                        compound['excluded_prefixes'].append(alias)
                if role in local_numbers:
                    local_numbers[role][alias] = value
                key = (role, alias)
                if key in meanings and meanings[key] != value:
                    raise ValueError(f'{path}: conflicting {role} meanings for {alias!r}')
                if key in meanings:
                    continue
                meanings[key] = value
                if role in LISTS:
                    values.append(alias)
                elif role in NUMBERS:
                    values[alias] = value
                else:
                    values.setdefault(value, []).append(alias)
        if compound is not None:
            for role, key in (('multipliers', 'multiplier'), ('scales', 'scale')):
                value = local_numbers[role].get(compound[key])
                if value is None:
                    raise ValueError(f'{path}: compound {key} must name an alias in {role}')
                compound[key + '_value'] = value
            if not all(compound[key] for key in ('ones', 'small', 'tens')):
                raise ValueError(f'{path}: compound numbers require ones, small and tens aliases')

    def lua(value):
        if isinstance(value, str):
            return json.dumps(value, ensure_ascii=False)
        if isinstance(value, int):
            return str(value)
        if isinstance(value, list):
            return '{' + ', '.join(lua(alias) for alias in value) + '}'
        return '{\n' + ''.join(f'  [{lua(key)}] = {lua(item)},\n'
                                 for key, item in sorted(value.items())) + '}'

    data = {'lexicon': vocabulary, 'languages': languages, 'normalization': mappings}
    return '-- Generated from assistant/vocabulary/*.xml; do not edit.\nreturn ' + lua(data) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
