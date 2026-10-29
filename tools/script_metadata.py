#!/usr/bin/env python3
"""Derive native module discovery and LuaLS declarations from .cgen contracts."""
import argparse
import pathlib
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--registry', required=True)
parser.add_argument('--annotations', required=True)
parser.add_argument('modules', nargs='+')
args = parser.parse_args()
registry = ['// Generated from .cgen. Do not edit.', '#include <lua.h>', '#include <lauxlib.h>']
modules = []
annotations = pathlib.Path(args.annotations)
annotations.mkdir(parents=True, exist_ok=True)
for filename in sorted(args.modules):
    root = ET.parse(filename).getroot()
    name = root.attrib['name']
    symbol = root.get('luaopen', 'luaopen_' + re.sub(r'[^a-zA-Z0-9_]', '_', name))
    modules.append((name, symbol))
    registry.append(f'int {symbol}(lua_State *L);')
    lines = ['---@meta', '-- Generated from ' + filename + '. Do not edit.', 'local M = {}']
    types = {'int': 'integer', 'size': 'integer', 'number': 'number', 'bool': 'boolean', 'string': 'string', 'function': 'function', 'table': 'table', 'value': 'any'}
    for function in root.findall('./functions/function'):
        params = []
        summary = function.findtext('summary')
        if summary:
            lines.append('---' + summary.replace('\n', ' '))
        for argument in function.findall('arg'):
            if argument.get('output') or argument.get('bufsize'):
                continue
            param = argument.attrib['name']
            params.append(param)
            optional = '?' if argument.get('optional') == 'true' or argument.get('default') is not None else ''
            lines.append(f"---@param {param}{optional} {types[argument.attrib['type']]}")
        result = function.find('returns')
        if result is not None and result.get('type') != 'void':
            lines.append('---@return ' + types.get(result.get('type'), 'any ...'))
        lua_name = function.get('name')
        # Match cgen's camelCase naming convention.
        lua_name = re.sub(r'_([a-z])', lambda m: m[1].upper(), lua_name)
        lua_name = function.get('export', lua_name)
        lines.append(f'M["{lua_name}"] = function({", ".join(params)}) end')
    lines.append('return M')
    (annotations / (name + '.lua')).write_text('\n'.join(lines) + '\n')
registry.append('static void script_preload_generated(lua_State *L) {')
for name, symbol in modules:
    registry.append(f'  lua_pushcfunction(L, {symbol}); lua_setfield(L, -2, "{name}");')
registry.append('}')
pathlib.Path(args.registry).write_text('\n'.join(registry) + '\n')
