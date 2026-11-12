#!/usr/bin/env python3
"""Reviewed local Orion plugin releases: immutable storage and atomic activation."""
import argparse
import base64
import binascii
import copy
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]*\Z")
RELEASE = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+\Z")
LIMIT = 64 * 1024 * 1024
PACKAGE_FORMAT = 1


def identifier(value):
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise ValueError("invalid package identifier")
    return value


def metadata(value):
    if (not isinstance(value, dict) or type(value.get("format")) is not int or
            value["format"] != PACKAGE_FORMAT or value.get("kind") != "plugin"):
        raise ValueError("unsupported package metadata")
    identifier(value.get("id")); identifier(value.get("publisher"))
    if not isinstance(value.get("release"), str) or not RELEASE.fullmatch(value["release"]):
        raise ValueError("release must be major.minor.patch")
    if type(value.get("runtime_api")) is not int or value["runtime_api"] != 1:
        raise ValueError("unsupported runtime API")
    if type(value.get("interface")) is not int or value["interface"] < 1:
        raise ValueError("invalid interface version")
    if not isinstance(value.get("dependencies"), dict):
        raise ValueError("dependencies must be an object")
    for name, version in value["dependencies"].items():
        identifier(name)
        if type(version) is not int or version < 1:
            raise ValueError("invalid dependency version")
    return value


def read_archive(path):
    contents = {}
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        if len(infos) > 4096 or sum(i.file_size for i in infos) > LIMIT:
            raise ValueError("package exceeds limits")
        folded = set()
        for info in infos:
            name = info.filename
            parts = PurePosixPath(name).parts
            if (not parts or name.startswith("/") or "\\" in name or ":" in name
                    or any(p in (".", "..") for p in parts) or PurePosixPath(name).as_posix() != name
                    or info.is_dir() or stat.S_ISLNK(info.external_attr >> 16)
                    or name.casefold() in folded or info.file_size > 16 * 1024 * 1024):
                raise ValueError(f"invalid archive entry: {name}")
            folded.add(name.casefold())
            contents[name] = archive.read(info)
    inventory = json.loads(contents.pop("SHA256SUMS.json"))
    if not isinstance(inventory, dict) or set(inventory) != set(contents):
        raise ValueError("archive inventory mismatch")
    for name, data in contents.items():
        if hashlib.sha256(data).hexdigest() != inventory[name]:
            raise ValueError(f"checksum mismatch: {name}")
    if not {"package.json", "manifest.lua", "main.lua"} <= contents.keys():
        raise ValueError("plugin requires package.json, manifest.lua and main.lua")
    return metadata(json.loads(contents["package.json"])), contents


def find_tool(name):
    choices = [os.environ["ORION_" + name.upper()]] if "ORION_" + name.upper() in os.environ else [name + "5.4", name + "-5.4", name]
    for choice in choices:
        found = shutil.which(choice)
        if found: return found
    raise ValueError(f"{name} is required for package validation")


def validate_contents(info, contents):
    if "settings.lua" in contents: raise ValueError("settings.lua is reserved for package configuration")
    with tempfile.TemporaryDirectory(prefix="orion-package-check-") as temporary:
        root = Path(temporary)
        for relative, data in contents.items():
            path = root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            if path.suffix == ".lua":
                subprocess.run([find_tool("luac"), "-p", str(path)], check=True, capture_output=True, text=True, timeout=10)
        result = subprocess.run([find_tool("lua"), str(ROOT / "tools/sdk/validate_package.lua"), str(root)],
                                check=True, capture_output=True, text=True, timeout=10)
    lines = [line.split("\t") for line in result.stdout.splitlines()]
    expected = ["ROOT", info["id"], "1", str(info["interface"])]
    if not lines or lines[0] != expected:
        raise ValueError("package metadata disagrees with plugin manifest")
    dependencies = {row[1]: int(row[2]) for row in lines[1:] if len(row) == 3 and row[0] == "DEP"}
    if dependencies != info["dependencies"]:
        raise ValueError("package dependencies disagree with plugin manifest")


def content_digest(contents):
    return hashlib.sha256(b"".join(name.encode() + b"\0" + hashlib.sha256(data).digest()
                                    for name, data in sorted(contents.items()))).hexdigest()


def signature_payload(digest):
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("invalid package digest")
    return b"Orion-Package-v1\0" + bytes.fromhex(digest)


def openssl(*arguments, input_data=None):
    executable = shutil.which("openssl")
    if not executable: raise ValueError("OpenSSL 3 or later is required for package signatures")
    if input_data is None:
        return subprocess.run([executable, *arguments], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, check=True, timeout=15).stdout
    with tempfile.TemporaryDirectory(prefix="orion-signature-input-") as temporary:
        source = Path(temporary) / "payload"; source.write_bytes(input_data)
        return subprocess.run([executable, *arguments, "-in", str(source)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, check=True, timeout=15).stdout


def public_key(path, private=False):
    args = ["pkey", "-in", str(path), "-outform", "DER"]
    args += ["-pubout"] if private else ["-pubin"]
    der = openssl(*args)
    if len(der) != 44 or not der.startswith(bytes.fromhex("302a300506032b6570032100")):
        raise ValueError("publisher key must be Ed25519")
    return der


def sign_archive(archive, private_key, output):
    info, contents = read_archive(archive)
    validate_contents(info, contents)
    digest = content_digest(contents)
    public = public_key(private_key, private=True)
    signature = openssl("pkeyutl", "-sign", "-rawin", "-inkey", str(private_key), input_data=signature_payload(digest))
    if len(signature) != 64: raise ValueError("invalid Ed25519 signature")
    envelope = {"format": 1, "algorithm": "Ed25519", "fingerprint": hashlib.sha256(public).hexdigest(),
                "signature": base64.b64encode(signature).decode("ascii")}
    with Path(output).open("x", encoding="utf-8") as file:
        json.dump(envelope, file, sort_keys=True, indent=2); file.write("\n")
        file.flush(); os.fsync(file.fileno())


def verify_signature(current, info, digest, envelope):
    policy = current.get("signature_policy", "optional")
    if policy not in ("optional", "require"): raise ValueError("invalid signature policy in store")
    required = policy == "require"
    if envelope is None:
        if required: raise ValueError("signature required")
        return
    if not isinstance(envelope, dict) or set(envelope) != {"format", "algorithm", "fingerprint", "signature"} or \
       type(envelope["format"]) is not int or envelope["format"] != 1 or envelope["algorithm"] != "Ed25519":
        raise ValueError("invalid signature envelope")
    pin = current.get("trusted_publishers", {}).get(info["publisher"])
    if not isinstance(pin, dict) or envelope["fingerprint"] != pin.get("fingerprint"):
        raise ValueError("publisher key is not trusted")
    try:
        key = base64.b64decode(pin["public_key"], validate=True)
        signature = base64.b64decode(envelope["signature"], validate=True)
    except (KeyError, ValueError, TypeError, binascii.Error) as error:
        raise ValueError("invalid signature encoding") from error
    if len(key) != 44 or not key.startswith(bytes.fromhex("302a300506032b6570032100")) or \
       hashlib.sha256(key).hexdigest() != pin["fingerprint"] or len(signature) != 64:
        raise ValueError("invalid trusted key or signature")
    with tempfile.TemporaryDirectory(prefix="orion-signature-") as temporary:
        pub = Path(temporary) / "public.der"; pub.write_bytes(key)
        sig = Path(temporary) / "signature"; sig.write_bytes(signature)
        try:
            openssl("pkeyutl", "-verify", "-rawin", "-pubin", "-keyform", "DER", "-inkey", str(pub),
                    "-sigfile", str(sig), input_data=signature_payload(digest))
        except subprocess.CalledProcessError as error:
            raise ValueError("invalid publisher signature") from error


def read_object(directory, digest):
    signature_payload(digest)
    root = directory / "objects" / digest
    if root.is_symlink() or not root.is_dir(): raise ValueError("missing or linked package object")
    result = {}; total = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink(): raise ValueError("linked package object")
        if path.is_dir(): continue
        if not path.is_file(): raise ValueError("invalid package object")
        name = path.relative_to(root).as_posix()
        if len(result) >= 4096 or path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError("package object exceeds limits")
        data = path.read_bytes(); total += len(data)
        if total > LIMIT: raise ValueError("package object exceeds limits")
        result[name] = data
    if content_digest(result) != digest: raise ValueError("package object checksum mismatch")
    return result


def verify_selected(directory, current):
    for name, release in current["active"].items():
        item = current["installed"][name][release]
        contents = read_object(directory, item["digest"])
        info = metadata(json.loads(contents["package.json"]))
        if info != item["metadata"] or info["id"] != name or info["release"] != release:
            raise ValueError("installed metadata does not match package object")
        verify_signature(current, info, item["digest"], item.get("signature"))


def verify_snapshot(directory, current):
    generation = current.get("generation")
    if not isinstance(generation, str) or not re.fullmatch(r"[0-9a-f]{32}", generation):
        raise ValueError("invalid activated generation")
    snapshot = directory / "snapshots" / generation
    if snapshot.is_symlink() or not snapshot.is_dir(): raise ValueError("missing or linked snapshot")
    expected = {}
    for name, release in current["active"].items():
        item = current["installed"][name][release]; info = item["metadata"]
        contents = read_object(directory, item["digest"])
        contents["manifest.lua"] = ("return " + lua_value({"id": name, "api": 1, "version": info["interface"],
                                                           "dependencies": info["dependencies"]}) + "\n").encode()
        contents["settings.lua"] = ("return " + lua_value(current.get("configuration", {}).get(name, {})) + "\n").encode()
        for path, data in contents.items(): expected[f"orion/plugins/{name}/{path}"] = hashlib.sha256(data).digest()
    actual = {}
    for path in snapshot.rglob("*"):
        if path.is_symlink(): raise ValueError("linked file in activated snapshot")
        if path.is_dir(): continue
        if not path.is_file(): raise ValueError("invalid activated snapshot")
        name = path.relative_to(snapshot).as_posix()
        if name not in expected or path.stat().st_size > 16 * 1024 * 1024: raise ValueError("unexpected snapshot file")
        actual[name] = hashlib.sha256(path.read_bytes()).digest()
    if actual != expected: raise ValueError("activated snapshot checksum mismatch")


@contextmanager
def locked(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "manager.lock").open("a+b") as handle:
        if os.name == "nt":
            import msvcrt
            handle.seek(0); handle.write(b"0"); handle.flush(); handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == "nt":
                handle.seek(0); msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle, fcntl.LOCK_UN)


def state(directory):
    path = directory / "state.json"
    return json.loads(path.read_text()) if path.exists() else {"format": 1, "installed": {}, "active": {}, "previous": None}


def builtin_versions(lua_root=None):
    plugins = (lua_root or ROOT / "share/lua") / "orion/plugins"
    if not plugins.is_dir():
        raise ValueError("bundled plugin directory is missing")
    versions = {}
    for directory in sorted(plugins.iterdir()):
        if not directory.is_dir():
            continue
        name = identifier(directory.name)
        try:
            result = subprocess.run([find_tool("lua"), str(ROOT / "tools/sdk/validate_package.lua"), str(directory)],
                                    check=True, capture_output=True, text=True, timeout=10)
            root = result.stdout.splitlines()[0].split("\t")
            if len(root) != 4 or root[:3] != ["ROOT", name, "1"] or int(root[3]) < 1:
                raise ValueError("manifest identity or version mismatch")
            versions[name] = int(root[3])
        except (IndexError, ValueError, subprocess.SubprocessError) as error:
            raise ValueError(f"invalid bundled plugin manifest: {name}") from error
    return versions


def validate_selection(current, lua_root=None):
    versions = builtin_versions(lua_root)
    selected = {}
    for name, release in current["active"].items():
        if name in versions:
            raise ValueError(f"cannot override bundled plugin: {name}")
        item = current["installed"][name][release]
        selected[name] = item
        versions[name] = item["metadata"]["interface"]
    visiting, visited = set(), set()
    def visit(name):
        if name in visiting:
            raise ValueError(f"dependency cycle: {name}")
        if name in visited or name not in selected:
            return
        visiting.add(name)
        for dependency, required in selected[name]["metadata"]["dependencies"].items():
            if versions.get(dependency) != required:
                raise ValueError(f"{name} requires {dependency}@{required}")
            visit(dependency)
        visiting.remove(name); visited.add(name)
    for name in selected:
        visit(name)


def lua_value(value):
    if value is None: return "nil"
    if isinstance(value, bool): return "true" if value else "false"
    if isinstance(value, (int, float)):
        if not math.isfinite(value) or abs(value) > 9007199254740991:
            raise ValueError("configuration number outside exact portable range")
        return repr(value)
    if isinstance(value, str):
        escaped = "".join("\\%03d" % c if c < 32 or c in (34, 92, 127) else chr(c) for c in value.encode("utf-8"))
        return '"' + escaped.encode("latin-1").decode("utf-8") + '"'
    if isinstance(value, list): return "{" + ",".join(lua_value(v) for v in value) + "}"
    if isinstance(value, dict) and all(isinstance(k, str) for k in value):
        return "{" + ",".join("[" + lua_value(k) + "]=" + lua_value(v) for k, v in sorted(value.items())) + "}"
    raise ValueError("configuration must contain JSON values")


def sync_directory(directory):
    if os.name != "nt":
        descriptor = os.open(directory, os.O_RDONLY)
        try: os.fsync(descriptor)
        finally: os.close(descriptor)


def sync_tree(directory):
    for path in directory.rglob("*"):
        if path.is_file():
            with path.open("rb") as handle: os.fsync(handle.fileno())
    if os.name != "nt":
        for path in list(directory.rglob("*"))[::-1] + [directory]:
            if path.is_dir():
                descriptor = os.open(path, os.O_RDONLY)
                try: os.fsync(descriptor)
                finally: os.close(descriptor)


class PublicationUncertainError(OSError):
    """The new registry is visible, but its directory sync failed."""


def publish(directory, current):
    validate_selection(current)
    verify_selected(directory, current)
    generation = uuid.uuid4().hex
    snapshot = directory / "snapshots" / generation
    snapshot.mkdir(parents=True)
    published = False
    try:
        for name, release in current["active"].items():
            item = current["installed"][name][release]
            target = snapshot / "orion/plugins" / name
            shutil.copytree(directory / "objects" / item["digest"], target)
            info = item["metadata"]
            manifest = {"id": name, "api": 1, "version": info["interface"], "dependencies": info["dependencies"]}
            (target / "manifest.lua").write_bytes(("return " + lua_value(manifest) + "\n").encode("utf-8"))
            settings = current.get("configuration", {}).get(name, {})
            (target / "settings.lua").write_bytes(("return " + lua_value(settings) + "\n").encode("utf-8"))
        sync_tree(snapshot)
        sync_directory(snapshot.parent)
        current["generation"] = generation
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", dir=directory, delete=False) as handle:
                temporary = Path(handle.name)
                json.dump(current, handle, sort_keys=True, indent=2)
                handle.flush(); os.fsync(handle.fileno())
            os.replace(temporary, directory / "state.json")
            published = True
            try:
                sync_directory(directory)
            except OSError as error:
                raise PublicationUncertainError(
                    "registry published but directory sync failed; inspect the active profile before retrying"
                ) from error
        finally:
            if temporary is not None: temporary.unlink(missing_ok=True)
    except BaseException:
        if not published: shutil.rmtree(snapshot)
        raise


def install(directory, archive, signature_path=None, check_only=False):
    info, contents = read_archive(archive)
    validate_contents(info, contents)
    name, release = info["id"], info["release"]
    digest = content_digest(contents)
    envelope = None
    if signature_path is not None:
        signature_path = Path(signature_path)
        if signature_path.stat().st_size > 8192: raise ValueError("signature envelope too large")
        envelope = json.loads(signature_path.read_text(encoding="utf-8"))
    with locked(directory):
        current = state(directory)
        releases = current["installed"].setdefault(name, {})
        if any(item["metadata"]["publisher"] != info["publisher"] for item in releases.values()):
            raise ValueError("publisher differs from installed package")
        if release in releases and releases[release]["digest"] != digest:
            raise ValueError("release already exists with different contents")
        if envelope is None and release in releases:
            envelope = releases[release].get("signature")
        verify_signature(current, info, digest, envelope)
        releases[release] = {"digest": digest, "metadata": info, "signature": envelope}
        current["previous"] = {"active": dict(current["active"]), "configuration": copy.deepcopy(current.get("configuration", {}))}
        current["active"][name] = release
        validate_selection(current)
        if check_only:
            # The candidate is still an archive; verify the other selected objects.
            others = dict(current, active={key: value for key, value in current["active"].items() if key != name})
            verify_selected(directory, others)
            if (directory / "objects" / digest).exists(): read_object(directory, digest)
            return {"id": name, "release": release, "digest": digest}
        destination = directory / "objects" / digest
        if not destination.exists():
            destination.parent.mkdir(exist_ok=True)
            staging = Path(tempfile.mkdtemp(dir=destination.parent))
            try:
                for relative, data in contents.items():
                    path = staging / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                sync_tree(staging)
                os.replace(staging, destination)
                sync_directory(destination.parent)
            finally:
                if staging.exists(): shutil.rmtree(staging)
        publish(directory, current)


def select(directory, name=None, release=None, disable=False, rollback=False):
    with locked(directory):
        current = state(directory)
        previous = {"active": dict(current["active"]), "configuration": copy.deepcopy(current.get("configuration", {}))}
        if rollback:
            if current["previous"] is None:
                raise ValueError("no previous activation")
            current["active"] = current["previous"]["active"]
            current["configuration"] = current["previous"]["configuration"]
        elif disable:
            identifier(name)
            if name not in current["active"]:
                raise ValueError("plugin is not active")
            del current["active"][name]
        else:
            identifier(name)
            if release not in current["installed"].get(name, {}):
                raise ValueError("release is not installed")
            current["active"][name] = release
        current["previous"] = previous
        publish(directory, current)


def configure(directory, name, values):
    identifier(name)
    if not isinstance(values, dict): raise ValueError("configuration must be a JSON object")
    lua_value(values)
    with locked(directory):
        current = state(directory)
        if name not in current["installed"]: raise ValueError("plugin is not installed")
        current["previous"] = {"active": dict(current["active"]), "configuration": copy.deepcopy(current.get("configuration", {}))}
        current.setdefault("configuration", {})[name] = values
        publish(directory, current)


def trust_publisher(directory, publisher, public_path):
    identifier(publisher)
    key = public_key(public_path)
    pin = {"fingerprint": hashlib.sha256(key).hexdigest(), "public_key": base64.b64encode(key).decode("ascii")}
    with locked(directory):
        current = state(directory); trusted = current.setdefault("trusted_publishers", {})
        if publisher in trusted and trusted[publisher] != pin:
            raise ValueError("publisher already has a different pinned key; remove it explicitly first")
        trusted[publisher] = pin
        publish(directory, current)
    return pin["fingerprint"]


def untrust_publisher(directory, publisher):
    identifier(publisher)
    with locked(directory):
        current = state(directory); trusted = current.setdefault("trusted_publishers", {})
        if publisher not in trusted: raise ValueError("publisher is not trusted")
        del trusted[publisher]
        publish(directory, current)


def set_signature_policy(directory, policy):
    if policy not in ("optional", "require"): raise ValueError("invalid signature policy")
    with locked(directory):
        current = state(directory); current["signature_policy"] = policy
        publish(directory, current)


def diagnose(directory):
    current = state(directory)
    report = {"format": 1, "runtime_api": 1, "platform": sys.platform,
              "generation": current.get("generation"), "signature_policy": current.get("signature_policy", "optional"),
              "packages": []}
    for name, release in sorted(current["active"].items()):
        item = current["installed"][name][release]
        info = item["metadata"]
        signature = item.get("signature")
        report["packages"].append({"id": name, "release": release, "publisher": info["publisher"],
                                   "interface": info["interface"], "digest": item["digest"],
                                   "signature_fingerprint": signature.get("fingerprint") if isinstance(signature, dict) else None})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--store", type=Path)
    commands = parser.add_subparsers(dest="command", required=True)
    cmd = commands.add_parser("install"); cmd.add_argument("archive", type=Path); cmd.add_argument("--signature", type=Path)
    cmd = commands.add_parser("check-install"); cmd.add_argument("archive", type=Path); cmd.add_argument("--signature", type=Path)
    cmd = commands.add_parser("sign"); cmd.add_argument("archive", type=Path); cmd.add_argument("private_key", type=Path); cmd.add_argument("output", type=Path)
    cmd = commands.add_parser("trust"); cmd.add_argument("publisher"); cmd.add_argument("public_key", type=Path)
    cmd = commands.add_parser("untrust"); cmd.add_argument("publisher")
    cmd = commands.add_parser("policy"); cmd.add_argument("mode", choices=("optional", "require"))
    cmd = commands.add_parser("enable"); cmd.add_argument("id"); cmd.add_argument("release")
    cmd = commands.add_parser("disable"); cmd.add_argument("id")
    commands.add_parser("rollback"); commands.add_parser("list"); commands.add_parser("diagnose"); commands.add_parser("verify")
    cmd = commands.add_parser("configure"); cmd.add_argument("id"); cmd.add_argument("json_file", type=Path)
    cmd = commands.add_parser("run"); cmd.add_argument("executable"); cmd.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command != "sign" and args.store is None: parser.error("--store is required")
    directory = args.store.resolve() if args.store else None
    try:
        if args.command == "sign": sign_archive(args.archive, args.private_key, args.output)
        elif args.command == "trust": print(trust_publisher(directory, args.publisher, args.public_key))
        elif args.command == "untrust": untrust_publisher(directory, args.publisher)
        elif args.command == "policy": set_signature_policy(directory, args.mode)
        elif args.command == "install": install(directory, args.archive, args.signature)
        elif args.command == "check-install":
            print(json.dumps(install(directory, args.archive, args.signature, check_only=True), sort_keys=True, indent=2))
        elif args.command == "enable": select(directory, args.id, args.release)
        elif args.command == "disable": select(directory, args.id, disable=True)
        elif args.command == "configure": configure(directory, args.id, json.loads(args.json_file.read_text()))
        elif args.command == "rollback": select(directory, rollback=True)
        elif args.command == "diagnose": print(json.dumps(diagnose(directory), sort_keys=True, indent=2))
        elif args.command == "verify":
            with locked(directory):
                current = state(directory); validate_selection(current)
                verify_selected(directory, current); verify_snapshot(directory, current)
            print("Package profile verified")
        elif args.command == "list": print(json.dumps(state(directory), sort_keys=True, indent=2))
        elif args.command == "run":
            executable = shutil.which(args.executable)
            if not executable: raise ValueError("runtime executable was not found")
            lua_root = Path(executable).resolve().parent.parent / "share/orion/lua"
            if not lua_root.is_dir(): raise ValueError("cannot locate the launched runtime's Lua modules")
            with locked(directory):
                current = state(directory); validate_selection(current, lua_root)
                verify_selected(directory, current); verify_snapshot(directory, current)
                environment = dict(os.environ, ORION_PLUGIN_ROOT=str(directory / "snapshots" / current["generation"]))
            return subprocess.run([executable] + args.arguments, env=environment).returncode
    except (ValueError, KeyError, OSError, RuntimeError, NotImplementedError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        print(f"[orion-packages] {error}", file=sys.stderr); return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
