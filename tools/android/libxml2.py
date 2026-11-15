#!/usr/bin/env python3
"""Build a static libxml2 for one Android ABI with the NDK's CMake toolchain.

The archive is pinned by SHA-256. Output: <build-dir>/<abi>/deps/{include,lib}.
Other image and font libraries come from vendor/ and are compiled by
packaging/android/build.mk."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

NAME = 'libxml2-2.13.5'
URL = 'https://download.gnome.org/sources/libxml2/2.13/libxml2-2.13.5.tar.xz'
SHA256 = '74fc163217a3964257d3be39af943e08861263c4231f9ef5b496b6f6d4c7b2b6'
ABIS = ('arm64-v8a', 'x86_64')


def download(cache):
    archive = cache / 'downloads' / (NAME + '.tar.xz')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        archive.unlink()
    if not archive.exists():
        partial = archive.with_name(archive.name + '.partial')
        subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error', '--connect-timeout', '30',
                        '--retry', '3', '--retry-delay', '2', '--output', str(partial), URL], check=True)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != SHA256:
            partial.unlink()
            raise SystemExit('Downloaded ' + NAME + ' does not match its pinned SHA-256.')
        partial.replace(archive)
    source = cache / 'sources' / NAME
    if not (source / 'CMakeLists.txt').is_file():
        shutil.rmtree(source, ignore_errors=True)
        source.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            if hasattr(tarfile, 'data_filter'):
                tar.extractall(source.parent, filter='data')
            else:
                tar.extractall(source.parent)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, default=os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_LATEST_HOME'))
    parser.add_argument('--abi', choices=ABIS, required=True)
    parser.add_argument('--api', default='28')
    parser.add_argument('--build-dir', type=Path, default=Path('build/android'))
    args = parser.parse_args()
    if not args.ndk or not (args.ndk / 'build/cmake/android.toolchain.cmake').is_file():
        raise SystemExit('Set ANDROID_NDK_HOME (or --ndk) to an Android NDK.')
    cache = args.build_dir.resolve()
    prefix = cache / args.abi / 'deps'
    settings = {'ndk': str(args.ndk.resolve()), 'api': args.api, 'source': NAME}
    stamp = prefix / 'settings.json'
    if stamp.is_file() and json.loads(stamp.read_text()) == settings and (prefix / 'lib/libxml2.a').is_file():
        return
    source = download(cache)
    shutil.rmtree(prefix, ignore_errors=True)
    work = cache / args.abi / 'work-libxml2'
    shutil.rmtree(work, ignore_errors=True)
    options = ['-DCMAKE_TOOLCHAIN_FILE=' + str(args.ndk / 'build/cmake/android.toolchain.cmake'),
               '-DANDROID_ABI=' + args.abi, '-DANDROID_PLATFORM=android-' + args.api,
               '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
               '-DCMAKE_INSTALL_PREFIX=' + str(prefix)]
    options += ['-DLIBXML2_WITH_' + name + '=OFF' for name in
                ('PYTHON', 'ICONV', 'ICU', 'LZMA', 'ZLIB', 'PROGRAMS', 'TESTS', 'HTTP', 'FTP', 'MODULES')]
    subprocess.run(['cmake', '-S', str(source), '-B', str(work), *options], check=True)
    subprocess.run(['cmake', '--build', str(work), '--parallel', str(os.cpu_count() or 2)], check=True)
    subprocess.run(['cmake', '--install', str(work)], check=True)
    shutil.rmtree(work, ignore_errors=True)
    stamp.write_text(json.dumps(settings))


if __name__ == '__main__':
    main()
