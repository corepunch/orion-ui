#!/usr/bin/env python3
"""Xcode build phases, independent of the Makefile's outputs."""
import importlib.util
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys

from generate import unity_sources, write


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def forms(root, output):
    for name, sources in unity_sources(root, 'groove').items():
        write(output / name, ''.join(f'#include "{p.relative_to(root).as_posix()}"\n' for p in sources))
    host = output / 'host'
    host.mkdir(exist_ok=True)
    tool = host / 'orionc'
    source = root / 'tools/orionc.c'
    inputs = [source, *root.glob('orion/**/*.h')]
    if not tool.exists() or any(p.stat().st_mtime > tool.stat().st_mtime for p in inputs):
        sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
        run('xcrun', '--sdk', 'macosx', 'clang', '-target', os.uname().machine + '-apple-macos12.0',
            '-std=c11', '-O2', '-I' + str(root),
            '-isysroot', sdk, '-I' + sdk + '/usr/include/libxml2', source, '-lxml2', '-o', tool)
    form = root / 'apps/groove/groove.orion'
    header = output / 'include/build/generated/apps/groove/groove.h'
    if not header.exists() or max(form.stat().st_mtime, tool.stat().st_mtime) > header.stat().st_mtime:
        header.parent.mkdir(parents=True, exist_ok=True)
        run(tool, '--input', form, '--output', header, '--prefix', 'groove')


def resources(root, output):
    bundle = Path(os.environ['TARGET_BUILD_DIR']) / os.environ['WRAPPER_NAME']
    ipad = os.environ['PLATFORM_NAME'] != 'macosx'
    destination = bundle if ipad else bundle / 'Contents/Resources'
    for source, name in ((root / 'share', 'orion'), (root / 'apps/groove/share', 'groove')):
        target = destination / 'share' / name
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(source, target)
    if ipad:
        (bundle / 'bin').mkdir(exist_ok=True)
        catalog = output / 'icons/ipad/AppIcon.xcassets/AppIcon.appiconset'
        catalog.mkdir(parents=True, exist_ok=True)
        images = []
        for size, scale in ((20, 1), (20, 2), (29, 1), (29, 2), (40, 1), (40, 2), (76, 1), (76, 2), (83.5, 2), (1024, 1)):
            pixels = int(size * scale)
            filename = f'icon-{pixels}.png'
            if not (catalog / filename).exists() or (catalog / filename).stat().st_mtime < (root / 'apps/groove/share/icon.png').stat().st_mtime:
                subprocess.run(['sips', '-z', str(pixels), str(pixels), str(root / 'apps/groove/share/icon.png'),
                                '--out', str(catalog / filename)], check=True, stdout=subprocess.DEVNULL)
            images.append(dict(idiom='ios-marketing' if size == 1024 else 'ipad', size=f'{size}x{size}', scale=f'{scale}x', filename=filename))
        (catalog / 'Contents.json').write_text(json.dumps(dict(images=images, info=dict(version=1, author='Orion'))))
        partial = Path(os.environ['DERIVED_FILE_DIR']) / 'icon-info.plist'
        run('xcrun', 'actool', catalog.parent, '--compile', bundle, '--output-partial-info-plist', partial,
            '--app-icon', 'AppIcon', '--target-device', 'ipad', '--minimum-deployment-target', '16.0',
            '--platform', os.environ['PLATFORM_NAME'])
    else:
        link = bundle / 'Contents/share'
        if not link.is_symlink():
            link.symlink_to('Resources/share')
        spec = importlib.util.spec_from_file_location('macos_bundle', root / 'packaging/macos/bundle.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        icon = destination / 'groove.icns'
        if not icon.exists() or icon.stat().st_mtime < (root / 'apps/groove/share/icon.png').stat().st_mtime:
            module.make_icon(root / 'apps/groove/share/icon.png', destination, 'groove')


if __name__ == '__main__':
    {'forms': forms, 'resources': resources}[sys.argv[1]](Path(os.environ['ORION_ROOT']).resolve(), Path(os.environ['DERIVED_FILE_DIR']))
