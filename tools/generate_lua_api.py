#!/usr/bin/env python3
"""Generate Lua registration, argument guards, annotations and reference from XML."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def generate(source):
    root = ET.parse(source).getroot()
    module = root.attrib['module']
    methods = root.findall('function')
    names = set()
    header = ['/* Generated from ' + str(source) + '; do not edit. */']
    docs = ['# ' + module + ' Lua API', '', '<!-- Generated from ' + str(source) + '; do not edit. -->', '', root.findtext('description', '').strip(), '']
    lua = ['-- Generated; edit ' + str(source), '---@meta', '']
    guards = {'string': 'LUA_TSTRING', 'number': 'LUA_TNUMBER', 'table': 'LUA_TTABLE', 'function': 'LUA_TFUNCTION', 'boolean': 'LUA_TBOOLEAN'}
    for method in methods:
        name, impl = method.attrib['name'], method.attrib['impl']
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) or not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', impl) or name in names:
            raise ValueError('Invalid or duplicate function: ' + name)
        names.add(name)
        params = method.findall('param')
        signature = ', '.join(p.attrib['name'] + ('?' if p.get('optional') == 'true' else '') for p in params)
        summary = method.findtext('description', '').strip()
        if not summary:
            raise ValueError('Missing documentation: ' + name)
        docs += ['## `' + module + '.' + name + '(' + signature + ')`', '', summary, '', '| Argument | Type | Description |', '|---|---|---|']
        header += ['static int api_' + name + '(lua_State *L) {', f'  luaL_argcheck(L, lua_gettop(L) <= {len(params)}, {len(params) + 1}, "too many arguments");']
        optional_seen = False
        lua += ['---' + summary.replace('\n', '\n---')]
        for i, p in enumerate(params, 1):
            typ, pname = p.attrib['type'], p.attrib['name']
            optional = p.get('optional') == 'true'
            if optional_seen and not optional:
                raise ValueError('Required parameter follows optional: ' + name)
            optional_seen |= optional
            checks = [f'lua_isinteger(L, {i})' if t == 'integer' else f'lua_type(L, {i}) == {guards[t]}' for t in typ.split('|')]
            check = f'luaL_argcheck(L, ' + ' || '.join(checks) + f', {i}, "{typ} expected");'
            header += ['  ' + (f'if (!lua_isnoneornil(L, {i})) ' if optional else '') + check]
            docs += [f'| `{pname}` | `' + typ.replace('|', r'\|') + ('?`' if optional else '`') + ' | ' + (p.text or '').strip() + ' |']
            lua += ['---@param ' + pname + ('?' if optional else '') + ' ' + typ + ' ' + (p.text or '').strip()]
        returns = method.find('returns')
        docs += ['', '**Returns:** `' + returns.attrib['type'] + '` — ' + (returns.text or '').strip(), '']
        if returns.attrib['type'] != 'nil':
            lua += ['---@return ' + returns.attrib['type']]
        lua += ['function ' + module + '.' + name + '(' + ', '.join(p.attrib['name'] for p in params) + ') end', '']
        for example in method.findall('example'):
            docs += ['```lua', (example.text or '').strip(), '```', '']
        header += ['  return ' + impl + '(L);', '}', '']
    header += ['static const luaL_Reg kLedgerApi[] = {']
    header += ['  { ' + json.dumps(m.attrib['name']) + ', api_' + m.attrib['name'] + ' },' for m in methods]
    header += ['  { NULL, NULL }', '};', '']
    schema_path = root.get('schema')
    if schema_path:
        schema = ET.parse(schema_path).getroot()
        docs += ['# Record fields', '', 'Generated from `' + schema_path + '`. Read methods return these fields as Lua tables.', '']
        for db in schema.findall('./databases/database'):
            for table in db.findall('table'):
                docs += ['## `' + db.attrib['name'] + '.' + table.attrib['name'] + '`', '', '| Field | XML type | Relation |', '|---|---|---|']
                docs += ['| `' + f.attrib['name'] + '` | `' + f.attrib['type'] + '` | ' + f.get('relation', '') + ' |' for f in table.findall('field')]
                docs += ['']
    return {'header': '\n'.join(header), 'docs': '\n'.join(docs), 'annotations': '\n'.join(line.rstrip() for line in lua), 'site': '---\nlayout: default\ntitle: Ledger Lua API\nnav_exclude: true\n---\n\n' + '\n'.join(docs)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    for option in ('header', 'docs', 'annotations', 'site'):
        parser.add_argument('--' + option, type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs = generate(args.source)
    for kind, content in outputs.items():
        path = getattr(args, kind)
        if path is None:
            continue
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                parser.exit(1, 'Generated file is stale: ' + str(path) + '\n')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")


if __name__ == '__main__':
    main()
