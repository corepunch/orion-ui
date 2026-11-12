#!/usr/bin/env python3
"""Local authoring and reproducible source packaging for Orion (Python stdlib)."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ID = re.compile(r"[a-z][a-z0-9_]*\Z")
ALLOWED = {".orion", ".lua", ".xml", ".md", ".txt", ".html", ".css", ".svg", ".png", ".jpg", ".json"}


def files(root):
    result, folded = [], set()
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"symbolic links are not packaged: {path}")
        if path.is_file():
            relative = path.relative_to(root)
            name = relative.as_posix()
            if (path.suffix not in ALLOWED or any(p.startswith(".") for p in relative.parts) or
                    "\\" in name or ":" in name or name.casefold() in folded):
                raise ValueError(f"unexpected package file: {path}; use a clean source directory")
            folded.add(name.casefold())
            if path.stat().st_size > 16 * 1024 * 1024:
                raise ValueError(f"file exceeds 16 MiB: {path}")
            result.append(path)
    if len(result) > 4095:
        raise ValueError("package exceeds 4095 source files")
    if sum(p.stat().st_size for p in result) > 64 * 1024 * 1024:
        raise ValueError("package exceeds 64 MiB")
    return result


def tool(name):
    configured = os.environ.get("ORION_" + name.upper())
    candidates = [configured] if configured else [name + "5.4", name + "-5.4", name]
    for candidate in candidates:
        executable = shutil.which(candidate)
        if executable:
            return executable
    raise ValueError(f"{name} is required; set ORION_{name.upper()} to your runtime's Lua tool")


def check(root, plugin_root=None):
    root = root.resolve()
    if not root.is_dir():
        raise ValueError(f"package directory does not exist: {root}")
    sources = files(root)
    app, plugin = root / "application.orion", root / "manifest.lua"
    if app.exists() == plugin.exists():
        raise ValueError("package needs exactly one of application.orion or manifest.lua")
    lua, luac = tool("lua"), tool("luac")
    subprocess.run([lua, "-e", 'assert(_VERSION == "Lua 5.4" or _VERSION == "Lua 5.5", "Lua 5.4/5.5 required")'],
                   check=True, capture_output=True, text=True, timeout=10)
    interpreter = subprocess.run([lua, "-v"], check=True, capture_output=True, text=True)
    compiler = subprocess.run([luac, "-v"], check=True, capture_output=True, text=True)
    lua_version = re.search(r"Lua (\d+\.\d+)", interpreter.stdout + interpreter.stderr)
    compiler_version = re.search(r"Lua (\d+\.\d+)", compiler.stdout + compiler.stderr)
    if not lua_version or not compiler_version or lua_version.group(1) != compiler_version.group(1):
        raise ValueError("lua and luac versions differ; set ORION_LUA and ORION_LUAC")
    for path in sources:
        if path.suffix == ".lua":
            subprocess.run([luac, "-p", str(path)], check=True, capture_output=True, text=True)
    if plugin.exists():
        if not (root / "main.lua").is_file():
            raise ValueError("plugin is missing main.lua")
        subprocess.run([lua, str(ROOT / "tools/sdk/check_plugins.lua"), str(root), str(ROOT / "share/lua"), str(plugin_root or "")],
                       check=True, timeout=10, capture_output=True, text=True)
    else:
        visited, active, documents = set(), set(), []
        def read(path):
            path = path.resolve()
            if not path.is_relative_to(root):
                raise ValueError(f"include escapes package: {path}")
            if path in active:
                raise ValueError(f"include cycle: {path}")
            if path in visited:
                return
            if len(active) >= 32:
                raise ValueError("include nesting exceeds 32")
            data = path.read_bytes()
            if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
                raise ValueError(f"DTD/entities are not supported: {path}")
            tree = ET.fromstring(data)
            if tree.tag != "orion":
                raise ValueError(f"expected orion root: {path}")
            active.add(path)
            for include in tree.findall("include"):
                read(path.parent / include.attrib["file"])
            active.remove(path)
            visited.add(path)
            documents.append(tree)
        read(app)
        applications = [a for d in documents for a in d.findall("application")]
        if len(applications) != 1:
            raise ValueError("expected one application declaration")
        application = applications[0]
        if not ID.fullmatch(application.get("name", "")):
            raise ValueError("invalid application name")
        if application.get("runtime-api", "1") != "1":
            raise ValueError("unsupported runtime API requirement")
        forms = [f for d in documents for f in d.findall("./forms/form")]
        names = [f.get("name") for f in forms]
        if None in names or len(set(names)) != len(names):
            raise ValueError("missing or duplicate form name")
        if application.get("form") not in names:
            raise ValueError("initial form does not exist")
        for module in [application.get("model")] + [f.get("controller") for f in forms]:
            if module and (not ID.fullmatch(module) or not (root / (module + ".lua")).is_file()):
                raise ValueError(f"missing or invalid controller/model: {module}")
    return sources


def create(root, name, kind):
    if not ID.fullmatch(name):
        raise ValueError("name must match [a-z][a-z0-9_]*")
    root.mkdir(parents=True, exist_ok=False)
    if kind == "plugin":
        (root / "manifest.lua").write_text(f'return {{id = "{name}", api = 1, version = 1, dependencies = {{calendar = 1}}}}\n')
        (root / "main.lua").write_text('return function(dependencies)\n  local Plugin = {}\n  function Plugin.next_day(date) return dependencies.calendar.add(date, 1) end\n  return Plugin\nend\n')
    else:
        template = (ROOT / "tools/sdk/templates/application.orion").read_text()
        (root / "application.orion").write_text(template.replace("starter", name))
    if kind == "plugin":
        metadata = {"format": 1, "id": name, "publisher": "local", "release": "0.1.0",
                    "kind": "plugin", "runtime_api": 1, "interface": 1, "dependencies": {"calendar": 1}}
        (root / "package.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (root / "README.md").write_text(f"# {name}\n\nOrion {kind} source package.\n\nAdd your license, supported runtime revision, release notes and domain tests before distribution.\n")


def pack(root, output, plugin_root=None):
    root = root.resolve()
    sources = check(root, plugin_root)
    output = output.resolve()
    if output.is_relative_to(root.resolve()) or output.exists():
        raise ValueError("output must be a new file outside the source directory")
    if any(p.relative_to(root).as_posix().casefold() == "sha256sums.json" for p in sources):
        raise ValueError("SHA256SUMS.json is reserved for generated inventory")
    # Exclusive publication: never overwrite a release; remove partial output on failure.
    with tempfile.TemporaryFile() as temporary:
        inventory, total = {}, 0
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sources:
                info = zipfile.ZipInfo(path.relative_to(root).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                data = path.read_bytes()
                total += len(data)
                if len(data) > 16 * 1024 * 1024 or total > 64 * 1024 * 1024:
                    raise ValueError("package exceeds limits")
                inventory[info.filename] = hashlib.sha256(data).hexdigest()
                archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED)
            inventory_data = (json.dumps(inventory, sort_keys=True, indent=2) + "\n").encode()
            if len(inventory_data) > 16 * 1024 * 1024 or total + len(inventory_data) > 64 * 1024 * 1024:
                raise ValueError("package including inventory exceeds limits")
            info = zipfile.ZipInfo("SHA256SUMS.json", (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, inventory_data)
        temporary.seek(0)
        with output.open("xb") as target:
            try:
                shutil.copyfileobj(temporary, target)
            except BaseException:
                output.unlink(missing_ok=True)
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin-root", type=Path, help="additional installed plugin snapshot for dependency checks")
    commands = parser.add_subparsers(dest="command", required=True)
    for kind in ("app", "plugin"):
        command = commands.add_parser("new-" + kind)
        command.add_argument("directory", type=Path)
        command.add_argument("--name", required=True)
    command = commands.add_parser("check")
    command.add_argument("directory", type=Path)
    command = commands.add_parser("pack")
    command.add_argument("directory", type=Path)
    command.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command.startswith("new-"):
            create(args.directory, args.name, "plugin" if args.command == "new-plugin" else "application")
        elif args.command == "check":
            check(args.directory, args.plugin_root)
        else:
            pack(args.directory, args.output, args.plugin_root)
    except (ValueError, OSError, KeyError, ET.ParseError, subprocess.SubprocessError) as error:
        print(f"[orion-sdk] {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        return 1
    print(f"{args.command}: {args.directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
