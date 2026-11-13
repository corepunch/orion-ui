#!/usr/bin/env python3
"""Check that launch.xml still matches the Makefile.

Every `make X` in a target's build= or run= must name a target make knows, and
a command that starts a checkout file (build/bin/gitclient, python3 tools/x.py)
must name a file that exists or that make builds. Each ${param} is tried with
every one of its choices, so `make run-ledger-${lang}` checks every language.

Make's own database (`make -pq`) supplies the targets: the Makefile is
evaluated but nothing is built, so foreach/eval rules count like written ones.

Usage: launch_check.py [checkout]    (default: the current directory)
"""

import itertools
import os
import re
import shlex
import subprocess
import sys
import xml.etree.ElementTree as ET

INTERPRETERS = {"python", "python3", "sh", "bash", "zsh", "node", "ruby", "perl", "lua"}
PROBE = ".launch-check-database"


def make_targets(directory, makefile=None):
    cmd = ["make", "-pq", "--no-print-directory"]
    if makefile:
        cmd += ["-f", makefile]
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES")}
    out = subprocess.run(cmd + [PROBE], cwd=directory, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, errors="replace").stdout
    targets, skip_next = set(), False
    for line in out.splitlines():
        if line.startswith("# Not a target:"):
            skip_next = True
            continue
        m = re.match(r"^([^\s#=:%][^=:]*?)::?(?:\s|$)", line)
        if m:
            if not skip_next:
                targets.update(m.group(1).split())
            skip_next = False
    targets.discard(PROBE)
    return targets


def expansions(target):
    """Every command text the target can produce: one per combination of choices."""
    params = [p for p in target.findall("param")]
    options = []
    for p in params:
        choices = (p.get("choices") or "").split()
        default = p.get("default") or (choices[0] if choices else "")
        options.append(choices or [default])
    for combo in itertools.product(*options) if options else [()]:
        values = {p.get("name"): v for p, v in zip(params, combo)}
        yield values, lambda text: re.sub(r"\$\{(\w+)\}", lambda m: values.get(m.group(1), m.group(0)), text)


def segments(command):
    """Simple commands of a shell line, split on && || ; and |."""
    try:
        lexer = shlex.shlex(command, posix=True, punctuation_chars="&|;")
        lexer.whitespace_split = True
        words = list(lexer)
    except ValueError:
        return []
    seg = []
    for w in words:
        if w in ("&&", "||", ";", "|", "&"):
            if seg:
                yield seg
            seg = []
        else:
            seg.append(w)
    if seg:
        yield seg


def main():
    checkout = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    path = os.path.join(checkout, "launch.xml")
    if not os.path.exists(path):
        print(f"launch-check: no {path}; nothing to check")
        return 0
    root = ET.parse(path).getroot()
    cache, errors = {}, []

    def known(directory, makefile):
        key = (directory, makefile)
        if key not in cache:
            cache[key] = make_targets(directory, makefile)
        return cache[key]

    for target in root.findall("target"):
        name = target.get("name")
        base = os.path.normpath(os.path.join(checkout, target.get("dir") or "."))
        for values, expand in expansions(target):
            where = f"target '{name}'" + (f" ({', '.join(f'{k}={v}' for k, v in values.items())})" if values else "")
            for attr in ("build", "run"):
                for seg in segments(expand(target.get(attr) or "")):
                    while seg and re.match(r"^\w+=", seg[0]):   # VAR=value prefix
                        seg = seg[1:]
                    if not seg:
                        continue
                    if seg[0] == "make" or seg[0].endswith("/make"):
                        directory, makefile, goals, args = base, None, [], iter(seg[1:])
                        for a in args:
                            if a == "-C":
                                directory = os.path.join(directory, next(args, "."))
                            elif a == "-f":
                                makefile = next(args, None)
                            elif a.startswith("-") or "=" in a:
                                continue
                            else:
                                goals.append(a)
                        targets = known(directory, makefile)
                        for g in goals:
                            if g not in targets:
                                errors.append(f"{where}: {attr}= runs 'make {g}', which the Makefile does not define")
                        continue
                    program = seg[1] if seg[0] in INTERPRETERS and len(seg) > 1 else seg[0]
                    if "/" not in program or program.startswith(("/", "~", "$")):
                        continue
                    if os.path.exists(os.path.join(base, program)):
                        continue
                    rel = os.path.relpath(os.path.join(base, program), checkout)
                    if rel not in known(checkout, None):
                        errors.append(f"{where}: {attr}= starts '{program}', which neither exists nor is built by make")

    for e in errors:
        print(f"launch-check: {path}: {e}", file=sys.stderr)
    if errors:
        return 1
    print(f"launch-check: {len(root.findall('target'))} targets match the Makefile")
    return 0


if __name__ == "__main__":
    sys.exit(main())
