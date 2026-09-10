#!/usr/bin/env python3
"""Install a self-contained macOS Scener binary, dylibs and resources."""
import argparse
import datetime
from pathlib import Path
import platform
import shlex
import shutil
import subprocess


def dependencies(path):
    lines = subprocess.check_output(['otool', '-L', str(path)], text=True).splitlines()[1:]
    return [line.strip().split(' (', 1)[0] for line in lines]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('this deployment helper currently supports macOS only')
    app = Path(__file__).resolve().parent
    ui = app.parent.parent
    source = ui / 'build/bin/scener'
    if not source.is_file():
        parser.error('build first: make -C ' + str(ui) + ' build/bin/scener')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    bundle = args.prefix.resolve() / 'lib/scener' / stamp
    (bundle / 'bin').mkdir(parents=True)
    (bundle / 'lib').mkdir()
    deployed = bundle / 'bin/scener'
    shutil.copy2(source, deployed)
    queue = [(source, deployed)]
    copied = {}
    while queue:
        original, target = queue.pop(0)
        for dependency in dependencies(original):
            if dependency.startswith(('/System/', '/usr/lib/')):
                continue
            if dependency.startswith('@rpath/'):
                resolved = ui / 'build/lib' / Path(dependency).name
            elif dependency.startswith('@loader_path/'):
                resolved = original.parent / dependency.removeprefix('@loader_path/')
            else:
                resolved = Path(dependency)
                if not resolved.is_absolute():
                    resolved = ui / resolved
            resolved = resolved.resolve()
            if resolved == original.resolve():
                continue
            if not resolved.is_file():
                raise RuntimeError(f'cannot resolve {dependency} from {original}')
            name = Path(dependency).name
            destination = bundle / 'lib' / name
            if name not in copied:
                copied[name] = resolved
                shutil.copy2(resolved, destination)
                queue.append((resolved, destination))
            elif copied[name] != resolved:
                raise RuntimeError(f'conflicting dylib basename: {name}')
    for name in ('orion', 'scener'):
        shutil.copytree(ui / 'build/share' / name, bundle / 'share' / name)
    revision = subprocess.check_output(['git', '-C', str(app), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(app), 'status', '--short'], text=True)
    (bundle / 'BUILD.txt').write_text(f'Source: {app}\nRevision: {revision}\nBuild: make -C {ui} build/bin/scener\nWorking changes:\n{dirty}')
    launcher = args.prefix.resolve() / 'bin/scener'
    launcher.parent.mkdir(parents=True, exist_ok=True)
    temporary = launcher.with_name('scener.new')
    temporary.write_text('#!/bin/sh\nexport DYLD_LIBRARY_PATH=' + shlex.quote(str(bundle / 'lib')) + '\nexec ' + shlex.quote(str(deployed)) + ' "$@"\n')
    temporary.chmod(0o755)
    temporary.replace(launcher)
    print(launcher)
    print(bundle)


if __name__ == '__main__':
    main()
