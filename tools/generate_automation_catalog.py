#!/usr/bin/env python3
"""Generate searchable Lua node/recipe metadata and documentation from XML."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def generate(source):
    root = ET.parse(source).getroot()
    lua = ['-- Generated from ' + str(source) + '; do not edit.', 'local M = {']
    lua += ['  id = ' + json.dumps(root.attrib['id']) + ', version = ' + json.dumps(root.attrib['version']) + ',']
    docs = ['# Local automation catalog', '', '<!-- Generated from ' + str(source) + '; do not edit. -->', '', root.findtext('description', '').strip(), '']
    for kind, collection in (('node', 'nodes'), ('recipe', 'recipes')):
        lua += ['  ' + collection + ' = {']
        names = set()
        for item in root.findall(kind):
            name = item.attrib['id']
            if name in names:
                raise ValueError('Duplicate ID: ' + name)
            names.add(name)
            values = dict(item.attrib)
            values['description'] = item.findtext('description', '').strip()
            if not values['description']:
                raise ValueError('Missing description: ' + name)
            tags = [tag.text for tag in item.findall('tag')]
            aliases = [tag.text for tag in item.findall('alias')]
            fields = [k + ' = ' + json.dumps(v, ensure_ascii=False) for k, v in values.items()]
            for key, array in (('tags', tags), ('aliases', aliases)):
                fields += [key + ' = {' + ', '.join(json.dumps(v, ensure_ascii=False) for v in array) + '}']
            params = []
            for p in item.findall('param'):
                params += ['{name = ' + json.dumps(p.attrib['name']) + ', type = ' + json.dumps(p.attrib['type']) + ', optional = ' + ('true' if p.get('optional') == 'true' else 'false') + '}']
            fields += ['params = {' + ', '.join(params) + '}']
            lua += ['    {' + ', '.join(fields) + '},']
            docs += ['## ' + kind.title() + ' `' + name + '` — ' + values['title'], '', values['description'], '', 'Tags: ' + ', '.join(tags), '']
            if kind == 'node':
                docs += ['Ports: `' + values['input'] + '` → `' + values['output'] + '`.', '']
            if params:
                docs += ['| Parameter | Type | Required | Meaning |', '|---|---|---|---|']
                docs += ['| `' + p.attrib['name'] + '` | `' + p.attrib['type'] + '` | ' + ('no' if p.get('optional') == 'true' else 'yes') + ' | ' + (p.text or '').strip() + ' |' for p in item.findall('param')]
                docs += ['']
        lua += ['  },']
    lua += ['}', '']
    for kind, collection in (('node', 'nodes'), ('recipe', 'recipes')):
        lua += ['function M.bind_' + collection + '(impl)', '  return {']
        for item in root.findall(kind):
            implementation = item.attrib['impl']
            if not implementation.replace('_', '').isalnum():
                raise ValueError('Invalid implementation: ' + implementation)
            lua += ['    [' + json.dumps(item.attrib['id']) + '] = assert(impl.' + implementation + ', "missing implementation: ' + implementation + '"),']
        lua += ['  }', 'end', '']
    lua += ['function M.bind_api(impl, target)']
    for api in root.findall('api'):
        for function in api.findall('function'):
            name = function.attrib['name']
            params = function.findall('param')
            lua += ['  assert(type(impl.' + name + ') == "function", "missing API implementation: ' + name + '")', '  target.' + name + ' = function(...)', '    local args = table.pack(...)', '    assert(args.n <= ' + str(len(params)) + ', "too many arguments: ' + name + '")']
            for i, p in enumerate(params, 1):
                value = 'args[' + str(i) + ']'
                check = '(math.type(' + value + ') == "integer")' if p.attrib['type'] == 'integer' else '(type(' + value + ') == "' + p.attrib['type'] + '")'
                if p.get('optional') == 'true':
                    check = value + ' == nil or ' + check
                lua += ['    assert(' + check + ', "invalid argument: ' + p.attrib['name'] + '")']
            lua += ['    return impl.' + name + '(table.unpack(args, 1, args.n))', '  end']
    lua += ['end', '', 'return M', '']
    for api in root.findall('api'):
        for function in api.findall('function'):
            params = function.findall('param')
            signature = ', '.join(p.attrib['name'] + ('?' if p.get('optional') == 'true' else '') for p in params)
            docs += ['## `' + api.attrib['module'] + '.' + function.attrib['name'] + '(' + signature + ')`', '', function.findtext('description', '').strip(), '', 'Returns: `' + function.attrib['returns'] + '`.', '', '| Parameter | Type | Meaning |', '|---|---|---|']
            docs += ['| `' + p.attrib['name'] + '` | `' + p.attrib['type'] + '` | ' + (p.text or '').strip() + ' |' for p in params]
            docs += ['']
    return '\n'.join(lua), '\n'.join(docs)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--lua', type=Path, required=True)
    p.add_argument('--docs', type=Path, required=True)
    p.add_argument('--site', type=Path)
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    lua, docs = generate(a.source)
    outputs = [(a.lua, lua), (a.docs, docs)]
    if a.site:
        outputs.append((a.site, '---\nlayout: default\ntitle: Ledger automation catalog\nnav_exclude: true\n---\n\n' + docs))
    for path, content in outputs:
        if a.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                p.exit(1, 'Generated file is stale: ' + str(path) + '\n')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")


if __name__ == '__main__':
    main()
