#!/usr/bin/env python3
"""Xcode build phases, independent of the Makefile's outputs."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys

from common import theme_sheets, unity_sources, write


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def app_icon(root, output):
    source = root / 'apps/groove/share/icon.png'
    catalog = output / 'Assets.xcassets'
    appicons = catalog / 'AppIcon.appiconset'
    appicons.mkdir(parents=True, exist_ok=True)
    images = []
    for idiom, sizes in (
        ('mac', ((size, scale) for size in (16, 32, 128, 256, 512) for scale in (1, 2))),
        ('ipad', ((20, 1), (20, 2), (29, 1), (29, 2), (40, 1), (40, 2), (76, 1), (76, 2), (83.5, 2))),
        ('ios-marketing', ((1024, 1),)),
    ):
        for size, scale in sizes:
            pixels = int(size * scale)
            filename = f'icon-{pixels}.png'
            target = appicons / filename
            if not target.exists() or target.stat().st_mtime < source.stat().st_mtime:
                subprocess.run(['sips', '-z', str(pixels), str(pixels), str(source), '--out', str(target)],
                               check=True, stdout=subprocess.DEVNULL)
            images.append(dict(idiom=idiom, size=f'{size}x{size}', scale=f'{scale}x', filename=filename))
    info = dict(version=1, author='Orion')
    write(catalog / 'Contents.json', json.dumps(dict(info=info), indent=2) + '\n')
    write(appicons / 'Contents.json', json.dumps(dict(images=images, info=info), indent=2) + '\n')


def forms(root, output):
    for name, sources in unity_sources(root, 'groove').items():
        write(output / name, ''.join(f'#include "{p.relative_to(root).as_posix()}"\n' for p in sources))
    embed = root / 'tools/embed_text.py'
    for sheet in theme_sheets(root):
        name = sheet.stem.replace('-', '_')
        header = output / 'include/orion/themes' / (sheet.stem + '_css.h')
        if not header.exists() or max(embed.stat().st_mtime, sheet.stat().st_mtime) > header.stat().st_mtime:
            contents = subprocess.check_output([sys.executable, str(embed), str(sheet), f'k_{name}_css'], text=True)
            write(header, contents)
    freetype = root / 'vendor/freetype'
    components = ('base/ftbase', 'base/ftinit', 'base/ftsystem', 'base/ftglyph', 'base/ftbitmap',
                  'base/ftdebug', 'base/ftmm', 'gzip/ftgzip', 'autofit/autofit', 'truetype/truetype',
                  'cff/cff', 'psaux/psaux', 'psnames/psnames', 'pshinter/pshinter', 'sfnt/sfnt',
                  'smooth/smooth', 'sdf/sdf', 'raster/raster')
    archives = []
    for arch in os.environ['ARCHS'].split():
        folder = output / ('freetype-' + arch)
        folder.mkdir(parents=True, exist_ok=True)
        objects = []
        for component in components:
            obj = folder / (component.replace('/', '_') + '.o')
            run(os.environ.get('CC', 'clang'), '-arch', arch, '-isysroot', os.environ['SDKROOT'],
                '-std=c11', '-DFT2_BUILD_LIBRARY', '-DFT_CONFIG_MODULES_H="orion/user/freetype_modules.h"',
                '-I' + str(root), '-I' + str(freetype / 'include'),
                '-c', freetype / 'src' / (component + '.c'), '-o', obj)
            objects.append(obj)
        archive = folder / 'liborion_freetype.a'
        run('xcrun', 'libtool', '-static', '-o', archive, *objects)
        archives.append(archive)
    if len(archives) == 1:
        shutil.copyfile(archives[0], output / 'liborion_freetype.a')
    else:
        run('xcrun', 'lipo', '-create', *archives, '-output', output / 'liborion_freetype.a')
    image_components = {
        'libpng': 'png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil',
        'libjpeg': 'jaricom jcomapi jutils jerror jmemmgr jmemnobs jcapimin jcapistd jcarith jctrans jcparam jdatadst jcinit jcmaster jcmarker jcmainct jcprepct jccoefct jccolor jcsample jchuff jcdctmgr jfdctfst jfdctflt jfdctint jdapimin jdapistd jdarith jdtrans jdatasrc jdmaster jdinput jdmarker jdhuff jdmainct jdcoefct jdpostct jddctmgr jidctfst jidctflt jidctint jdsample jdcolor jquant1 jquant2 jdmerge',
        'zlib': 'adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil',
        'libnsbmp/src': 'libnsbmp',
        'bmplib': 'bmp-read bmp-write bmp-read-loadimage bmp-read-loadindexed bmp-read-icons bmp-common huffman logging',
    }
    bmplib_generated = output / 'bmplib-generated'
    bmplib_generated.mkdir(parents=True, exist_ok=True)
    generator = bmplib_generated / 'gen-huffman'
    host_sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    run('xcrun', '--sdk', 'macosx', 'clang', '-target', os.uname().machine + '-apple-macos12.0',
        '-isysroot', host_sdk, '-std=c11', root / 'vendor/bmplib/gen-huffman.c', '-o', generator)
    run(generator, bmplib_generated / 'huffman-codes.h')
    image_includes = [root / 'orion/user/image_deps_config', root / 'vendor/libpng', root / 'vendor/libjpeg',
                      root / 'vendor/libnsbmp/include', root / 'vendor/zlib', root / 'vendor/bmplib',
                      root / 'orion/user/bmplib_config', bmplib_generated]
    archives = []
    for arch in os.environ['ARCHS'].split():
        folder = output / ('image-deps-' + arch)
        folder.mkdir(parents=True, exist_ok=True)
        objects = []
        for directory, names in image_components.items():
            for name in names.split():
                obj = folder / (directory.replace('/', '_') + '_' + name + '.o')
                run(os.environ.get('CC', 'clang'), '-arch', arch, '-isysroot', os.environ['SDKROOT'],
                    '-std=c11', '-DNDEBUG', '-DPNG_ARM_NEON_OPT=0', *('-I' + str(p) for p in image_includes),
                    '-c', root / 'vendor' / directory / (name + '.c'), '-o', obj)
                objects.append(obj)
        archive = folder / 'liborion_image_deps.a'
        run('xcrun', 'libtool', '-static', '-o', archive, *objects)
        archives.append(archive)
    if len(archives) == 1:
        shutil.copyfile(archives[0], output / 'liborion_image_deps.a')
    else:
        run('xcrun', 'lipo', '-create', *archives, '-output', output / 'liborion_image_deps.a')
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
    app_icon(root, output)


def resources(root, output):
    bundle = Path(os.environ['TARGET_BUILD_DIR']) / os.environ['WRAPPER_NAME']
    ipad = os.environ['PLATFORM_NAME'] != 'macosx'
    destination = bundle if ipad else bundle / 'Contents/Resources'
    for source, name in ((root / 'share', 'orion'), (root / 'apps/groove/share', 'groove')):
        target = destination / 'share' / name
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(source, target)
    for localization in (root / 'apps/groove/share/locale').glob('*.lproj'):
        shutil.copytree(localization, destination / localization.name, dirs_exist_ok=True)
    if ipad:
        (bundle / 'bin').mkdir(exist_ok=True)
    else:
        # The Mac App Store rejects quarantine attributes on bundled resources.
        run('/usr/bin/xattr', '-dr', 'com.apple.quarantine', destination)
        link = bundle / 'Contents/share'
        if not link.is_symlink():
            link.symlink_to('Resources/share')


if __name__ == '__main__':
    {'forms': forms, 'resources': resources}[sys.argv[1]](Path(os.environ['ORION_ROOT']).resolve(), Path(os.environ['DERIVED_FILE_DIR']))
