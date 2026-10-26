#!/usr/bin/env python3
"""Generate executor capabilities, Lua registration and documentation from XML."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def generate(source):
    root = ET.parse(source).getroot()
    operations = root.findall('operation')
    if not operations:
        raise ValueError('No executor operations declared')
    schema = ET.parse(root.attrib['schema']).getroot()
    tables = {t.attrib['name']: t for t in schema.findall('./databases/database[@name="ib"]/table')}
    def check_fields(table_name, fields):
        table = tables[table_name]
        known = {f.attrib['name'] for f in table.findall('field')}
        if table.get('transient') == 'true' or any(f not in known or f == 'id' for f in fields.split(',') if f):
            raise ValueError('Invalid writable fields in ' + table_name)
        return table
    native = ['/* Generated from ' + str(source) + '; do not edit. */', 'static const ledger_executor_spec_t kExecutorSpecs[] = {']
    lua = ['-- Generated from ' + str(source) + '; do not edit.', 'return function(implementations)']
    docs = ['---', 'layout: default', 'title: Ledger executor operation contracts', 'nav_exclude: true', '---', '', '# Executor operation contracts', '', '<!-- Generated from ' + str(source) + '; do not edit. -->', '', 'Plans have optional `document = {field = value}` and `replace = {table = {rows...}}` sections. Only the fields and tables declared below are accepted. Parent references and new IDs are assigned by the host. All returned child fields are required. Unknown fields, invalid references/types, non-finite values and overlong strings fail atomically.', '']
    seen = set()
    for op in operations:
        name, impl = op.attrib['name'], op.attrib['impl']
        if name in seen or not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', impl):
            raise ValueError('Invalid or duplicate executor: ' + name)
        seen.add(name)
        patch, replace = op.find('document'), op.find('replace')
        fields = patch.get('fields', '') if patch is not None else ''
        table = replace.get('table', '') if replace is not None else ''
        parent = replace.get('parent', '') if replace is not None else ''
        line_fields = replace.get('fields', '') if replace is not None else ''
        for field in (fields + ',' + line_fields + ',' + table + ',' + parent).split(','):
            if field and not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', field):
                raise ValueError('Invalid field: ' + field)
        check_fields('documents', fields)
        if table:
            child = check_fields(table, line_fields)
            parent_field = child.find('field[@name="' + parent + '"]')
            if parent_field is None or parent_field.get('relation') != 'documents.id' or parent in line_fields.split(','):
                raise ValueError('Invalid owned child table: ' + table)
        if op.get('recalculate') not in ('true', 'false'):
            raise ValueError('recalculate must be true or false')
        recalc = op.get('recalculate') == 'true'
        native += ['  {' + ', '.join(json.dumps(s) for s in (name, fields, table, parent, line_fields)) + ', ' + str(recalc).lower() + '},']
        lua += ['  ledger.executor.register(' + json.dumps(name) + ', assert(implementations.' + impl + '))']
        docs += ['## `' + name + '`', '', op.findtext('summary', ''), '', '- Document patch fields: `' + (fields or 'none') + '`.', '- Replace child table: `' + (table or 'none') + '`; fields: `' + (line_fields or 'none') + '`.', '- Recalculate after apply: `' + str(recalc).lower() + '`.', '']
    native += ['};', '']
    lua += ['end', '']
    return '\n'.join(native), '\n'.join(lua), '\n'.join(docs)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    for name in ('header', 'lua', 'docs'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    for path, content in zip((a.header, a.lua, a.docs), generate(a.source)):
        if a.check:
            if not path.exists() or path.read_text(encoding='utf-8') != content:
                p.exit(1, 'Generated file is stale: ' + str(path) + '\n')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('w', encoding='utf-8', newline='\n') as output:
                output.write(content)


if __name__ == '__main__':
    main()
