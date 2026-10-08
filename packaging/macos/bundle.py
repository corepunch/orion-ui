#!/usr/bin/env python3
"""Package a native Orion executable as a self-contained macOS app bundle."""
import argparse
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


SYSTEM_PREFIXES = ('/System/Library/', '/usr/lib/', '/Library/Apple/System/Library/')
ICON_SIZES = (
    ('icon_16x16.png', 16), ('icon_16x16@2x.png', 32),
    ('icon_32x32.png', 32), ('icon_32x32@2x.png', 64),
    ('icon_128x128.png', 128), ('icon_128x128@2x.png', 256),
    ('icon_256x256.png', 256), ('icon_256x256@2x.png', 512),
    ('icon_512x512.png', 512), ('icon_512x512@2x.png', 1024),
)
DISPLAY_NAMES = {
    'browser': 'Browser', 'formeditor': 'Form Editor', 'gitclient': 'Git Client',
    'groove': 'Groove', 'imageeditor': 'Image Editor', 'penciltest': 'Pencil Test',
    'socialfeed': 'Social Feed', 'taskmanager': 'Task Manager', 'terminal': 'Terminal',
    'vibeoffice': 'Vibe Office',
}


def run(args, *, capture=False):
    return subprocess.run(args, check=True, text=capture, capture_output=capture)


def is_macho(path):
    if not path.is_file():
        return False
    result = subprocess.run(['file', '-b', str(path)], check=True, text=True, capture_output=True)
    return 'Mach-O' in result.stdout


def load_commands(path):
    result = run(['otool', '-l', str(path)], capture=True).stdout
    rpaths = []
    lines = result.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH' and i + 2 < len(lines):
            match = re.search(r'path (.+?) \(offset \d+\)', lines[i + 2].strip())
            if match:
                rpaths.append(match.group(1))
    return rpaths


def dependencies(path):
    result = run(['otool', '-L', str(path)], capture=True).stdout
    deps = []
    for line in result.splitlines()[1:]:
        match = re.match(r'\s+(.+?) \(compatibility version ', line)
        if match:
            deps.append(match.group(1))
    return deps


def expand_path(value, loader, origin, executable):
    if value.startswith('@loader_path/'):
        suffix = value[len('@loader_path/'):]
        return [loader.parent / suffix, origin.parent / suffix]
    if value.startswith('@executable_path/'):
        suffix = value[len('@executable_path/'):]
        return [executable.parent / suffix]
    if value.startswith('/'):
        return [Path(value)]
    return []


def resolve_dependency(value, loader, origin, executable, frameworks, runtime_libs):
    candidates = expand_path(value, loader, origin, executable)
    if value.startswith('@rpath/'):
        suffix = value[len('@rpath/'):]
        for rpath in load_commands(loader):
            for base in expand_path(rpath, loader, origin, executable):
                candidates.append(base / suffix)
        candidates.extend((frameworks / suffix, runtime_libs / suffix))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def set_rpath(path, wanted):
    if wanted not in load_commands(path):
        run(['install_name_tool', '-add_rpath', wanted, str(path)], capture=True)


def external_dependencies(executable, frameworks, runtime_libs):
    origins = {executable: executable}
    queue = [executable]
    queue.extend(path for path in frameworks.iterdir() if is_macho(path))
    for path in queue[1:]:
        origins[path] = runtime_libs / path.name

    processed = set()
    while queue:
        loader = queue.pop(0)
        if loader in processed:
            continue
        processed.add(loader)
        origin = origins.get(loader, loader)
        set_rpath(loader, '@executable_path/../lib' if loader == executable else '@loader_path')
        for dep in dependencies(loader):
            if dep.startswith(SYSTEM_PREFIXES):
                continue
            dep_path = Path(dep)
            if dep_path.is_absolute():
                resolved = dep_path.resolve() if dep_path.is_file() else None
            elif dep.startswith('@rpath/'):
                suffix = dep[len('@rpath/'):]
                bundled = frameworks / suffix
                if bundled.is_file():
                    continue
                resolved = resolve_dependency(dep, loader, origin, executable, frameworks, runtime_libs)
            else:
                resolved = resolve_dependency(dep, loader, origin, executable, frameworks, runtime_libs)
            if resolved is None:
                raise SystemExit(f'Cannot bundle non-system dependency {dep!r} used by {loader}')
            if '.framework/' in str(resolved):
                raise SystemExit(f'Cannot bundle non-system framework dependency {dep!r}; bundle it manually')

            dest = frameworks / resolved.name
            if dest.exists():
                if not is_macho(dest):
                    raise SystemExit(f'Bundle dependency path is not a Mach-O library: {dest}')
            else:
                shutil.copy2(resolved, dest)
                origins[dest] = resolved
                run(['install_name_tool', '-id', f'@rpath/{dest.name}', str(dest)], capture=True)
                set_rpath(dest, '@loader_path')
                queue.append(dest)
            run(['install_name_tool', '-change', dep, f'@rpath/{dest.name}', str(loader)], capture=True)


def make_icon(icon, resources, app):
    iconset = resources / f'{app}.iconset'
    iconset.mkdir()
    try:
        for filename, pixels in ICON_SIZES:
            run(['sips', '-z', str(pixels), str(pixels), str(icon),
                 '--out', str(iconset / filename)], capture=True)
        run(['iconutil', '-c', 'icns', '-o', str(resources / f'{app}.icns'), str(iconset)], capture=True)
    finally:
        shutil.rmtree(iconset, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--runtime-libs', type=Path, required=True)
    parser.add_argument('--share', type=Path, required=True)
    parser.add_argument('--icon', type=Path, required=True)
    parser.add_argument('--bundle-id', required=True)
    args = parser.parse_args()

    if not args.bundle.name.endswith('.app'):
        raise SystemExit('Bundle destination must end in .app')
    if not args.binary.is_file() or not args.icon.is_file():
        raise SystemExit('Expected both the built executable and app icon PNG')
    for path in (args.share / 'orion', args.share / args.source):
        if not path.is_dir():
            raise SystemExit(f'Missing built app resources: {path}')
    for tool in ('codesign', 'file', 'iconutil', 'install_name_tool', 'otool', 'sips'):
        if not shutil.which(tool):
            raise SystemExit(f'Required macOS packaging tool not found: {tool}')

    bundle = args.bundle.resolve()
    contents = bundle / 'Contents'
    macos = contents / 'MacOS'
    resources = contents / 'Resources'
    frameworks = contents / 'Frameworks'
    if bundle.exists():
        shutil.rmtree(bundle)
    macos.mkdir(parents=True)
    resources.mkdir()
    frameworks.mkdir()

    executable = macos / args.app
    shutil.copy2(args.binary, executable)
    share_root = resources / 'share'
    shutil.copytree(args.share / 'orion', share_root / 'orion')
    shutil.copytree(args.share / args.source, share_root / args.source)
    (contents / 'lib').symlink_to('Frameworks')
    (contents / 'share').symlink_to(Path('Resources') / 'share')

    for library in args.runtime_libs.iterdir():
        if is_macho(library):
            shutil.copy2(library, frameworks / library.name)
    external_dependencies(executable, frameworks, args.runtime_libs)
    make_icon(args.icon, resources, args.app)

    display_name = DISPLAY_NAMES.get(args.app, args.app.replace('-', ' ').title())
    info = {
        'CFBundleDevelopmentRegion': 'en',
        'CFBundleDisplayName': display_name,
        'CFBundleExecutable': args.app,
        'CFBundleIconFile': f'{args.app}.icns',
        'CFBundleIdentifier': args.bundle_id,
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundleName': display_name,
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': '1.0',
        'CFBundleVersion': '1',
        'LSApplicationCategoryType': 'public.app-category.developer-tools',
        'LSMinimumSystemVersion': '11.0',
        'NSHighResolutionCapable': True,
        'NSPrincipalClass': 'NSApplication',
    }
    (contents / 'Info.plist').write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_XML))
    (contents / 'PkgInfo').write_bytes(b'APPL????')
    run(['codesign', '--force', '--deep', '--sign', '-', str(bundle)], capture=True)
    run(['codesign', '--verify', '--deep', '--strict', str(bundle)], capture=True)
    print(f'Packaged {bundle}')


if __name__ == '__main__':
    main()
